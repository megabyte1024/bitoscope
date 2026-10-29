/*
 * SPDX-License-Identifier: MIT
 */

#include "hdf5_capture_writer.h"
#include "hdf5_lock.h"

#include <algorithm>
#include <cstddef>
#include <format>
#include <memory>
#include <new>
#include <system_error>
#include <thread>
#include <variant>
#include <vector>

#include <hdf5.h>

#include "chunk_deflater.h"
#include "read_samples.h"

namespace bitoscope::hdf5 {

namespace {

using bitoscope::capture::CaptureMetadata;
using bitoscope::capture::RawCaptureBuffer;
using bitoscope::capture::readSamples;

const char* shortReasonText(bitoscope::capture::ShortReason reason) {
  switch (reason) {
    case bitoscope::capture::ShortReason::StoppedByUser:
      return "stopped by user";
    case bitoscope::capture::ShortReason::StoppedWithSession:
      return "stopped with session";
    case bitoscope::capture::ShortReason::Memory:
      break;
  }
  return "memory";
}

std::optional<bitoscope::capture::ShortReason> sessionShortReason(
    std::span<const bitoscope::hdf5::SessionDeviceCapture> devices) {
  std::optional<bitoscope::capture::ShortReason> reason;
  for (const auto& device : devices) {
    const auto& shortfall = device.metadata.memory_shortfall;
    if (!shortfall) continue;
    if (shortfall->reason == bitoscope::capture::ShortReason::Memory) return shortfall->reason;
    if (!reason || shortfall->reason == bitoscope::capture::ShortReason::StoppedByUser) reason = shortfall->reason;
  }
  return reason;
}

constexpr uint32_t kFormatVersion = 4;
constexpr const char* kBitOrder = "lsb-first";
constexpr uint32_t kMaxGzipLevel = 9;
constexpr uint32_t kMaxAutoDeflateWorkers = 8;

uint32_t deflateWorkerCount(const Hdf5WriterOptions& options) {
  if (options.deflate_workers != 0) return options.deflate_workers;
  return (std::clamp)(std::thread::hardware_concurrency(), 1u, kMaxAutoDeflateWorkers);
}

class Handle {
 public:
  using Closer = herr_t (*)(hid_t);
  Handle() = default;
  Handle(hid_t id, Closer closer) : id_(id), closer_(closer) {}
  Handle(const Handle&) = delete;
  Handle& operator=(const Handle&) = delete;
  Handle(Handle&& other) noexcept : id_(other.id_), closer_(other.closer_) { other.id_ = -1; }
  ~Handle() {
    if (id_ >= 0 && closer_ != nullptr) closer_(id_);
  }
  hid_t id() const { return id_; }
  bool ok() const { return id_ >= 0; }
  bool close() {
    const bool good = id_ < 0 || closer_(id_) >= 0;
    id_ = -1;
    return good;
  }

 private:
  hid_t id_ = -1;
  Closer closer_ = nullptr;
};

std::unexpected<std::string> fail(std::string_view what) {
  return std::unexpected(std::format("HDF5: {}", what));
}

template <typename T>
std::optional<T> readU32LikeAttr(hid_t obj, const char* name, hid_t mem_type) {
  if (H5Aexists(obj, name) <= 0) return std::nullopt;
  Handle attr(H5Aopen(obj, name, H5P_DEFAULT), H5Aclose);
  if (!attr.ok()) return std::nullopt;
  T value{};
  if (H5Aread(attr.id(), mem_type, &value) < 0) return std::nullopt;
  return value;
}

// WHY: Pass UTF-8, not path::string(): vendored HDF5 widens a Windows name from UTF-8, not the ANSI code page.
std::string utf8Path(const std::filesystem::path& path) {
  const std::u8string text = path.u8string();
  return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}

template <typename T>
std::expected<void, std::string> writeScalarAttr(hid_t obj, const char* name, hid_t file_type,
                                                 hid_t mem_type, const T& value) {
  Handle space(H5Screate(H5S_SCALAR), H5Sclose);
  if (!space.ok()) return fail("cannot create scalar dataspace");
  Handle attr(H5Acreate2(obj, name, file_type, space.id(), H5P_DEFAULT, H5P_DEFAULT), H5Aclose);
  if (!attr.ok()) return fail(std::format("cannot create attribute {}", name));
  if (H5Awrite(attr.id(), mem_type, &value) < 0) {
    return fail(std::format("cannot write attribute {}", name));
  }
  return {};
}

std::expected<void, std::string> writeStringAttr(hid_t obj, const char* name,
                                                 const std::string& text) {
  Handle type(H5Tcopy(H5T_C_S1), H5Tclose);
  if (!type.ok()) return fail("cannot create string type");
  const size_t size = text.empty() ? 1 : text.size();
  if (H5Tset_size(type.id(), size) < 0 || H5Tset_strpad(type.id(), H5T_STR_NULLPAD) < 0) {
    return fail("cannot size string type");
  }
  if (H5Tset_cset(type.id(), H5T_CSET_UTF8) < 0) return fail("cannot set string charset");
  Handle space(H5Screate(H5S_SCALAR), H5Sclose);
  if (!space.ok()) return fail("cannot create scalar dataspace");
  Handle attr(H5Acreate2(obj, name, type.id(), space.id(), H5P_DEFAULT, H5P_DEFAULT), H5Aclose);
  if (!attr.ok()) return fail(std::format("cannot create attribute {}", name));
  std::string padded = text;
  padded.resize(size, '\0');
  if (H5Awrite(attr.id(), type.id(), padded.data()) < 0) {
    return fail(std::format("cannot write attribute {}", name));
  }
  return {};
}

std::expected<void, std::string> writeChannel(hid_t channels_group, const RawCaptureBuffer& buffer,
                                              uint32_t buffer_channel,
                                              const bitoscope::capture::SavedChannel& saved,
                                              uint64_t extent_first_sample,
                                              uint64_t total_samples, uint64_t total_bytes,
                                              const Hdf5WriterOptions& options) {
  const hsize_t dims = static_cast<hsize_t>(total_bytes);
  const hsize_t chunk = static_cast<hsize_t>((std::min)(options.chunk_bytes, total_bytes));

  Handle space(H5Screate_simple(1, &dims, nullptr), H5Sclose);
  if (!space.ok()) return fail("cannot create channel dataspace");
  Handle plist(H5Pcreate(H5P_DATASET_CREATE), H5Pclose);
  if (!plist.ok()) return fail("cannot create dataset property list");
  if (H5Pset_chunk(plist.id(), 1, &chunk) < 0) return fail("cannot set chunk size");
  if (options.gzip_level > 0 && H5Pset_deflate(plist.id(), options.gzip_level) < 0) {
    return fail("cannot enable gzip filter");
  }

  const std::string name = std::to_string(saved.index);
  Handle dataset(H5Dcreate2(channels_group, name.c_str(), H5T_STD_U8LE, space.id(), H5P_DEFAULT,
                            plist.id(), H5P_DEFAULT),
                 H5Dclose);
  if (!dataset.ok()) return fail(std::format("cannot create dataset for channel {}", name));

  if (auto r = writeScalarAttr(dataset.id(), "index", H5T_STD_U32LE, H5T_NATIVE_UINT32,
                               saved.index);
      !r) {
    return r;
  }
  if (auto r = writeStringAttr(dataset.id(), "name", saved.name); !r) return r;

  std::unique_ptr<ChunkDeflater> deflater;
  std::vector<uint8_t> piece;
  if (options.gzip_level > 0) {
    auto created = ChunkDeflater::create(deflateWorkerCount(options), options.gzip_level, static_cast<size_t>(chunk),
                                         options.before_deflate);
    if (!created) return std::unexpected(created.error());
    deflater = std::move(*created);
  } else {
    piece.resize(static_cast<size_t>(chunk));
  }
  const ChunkDeflater::Sink writeDeflated = [&](uint64_t index, std::span<const uint8_t> deflated) -> std::expected<void, std::string> {
    const hsize_t offset = static_cast<hsize_t>(index * chunk);
    if (H5Dwrite_chunk(dataset.id(), H5P_DEFAULT, 0, &offset, deflated.size(), deflated.data()) < 0) {
      return fail(std::format("cannot write channel {} at byte {}", name, offset));
    }
    return {};
  };

  for (uint64_t first = 0; first < total_bytes; first += chunk) {
    const uint64_t n = (std::min)(uint64_t{chunk}, total_bytes - first);
    const uint64_t sample_offset = extent_first_sample + 8 * first;
    const uint64_t samples_in_chunk = (std::min)(8 * n, total_samples - 8 * first);
    std::span<uint8_t> room(piece.data(), piece.size());
    if (deflater) {
      auto reserved = deflater->reserve(writeDeflated);
      if (!reserved) return std::unexpected(reserved.error());
      room = *reserved;
    }
    const std::span<uint8_t> out = room.first(static_cast<size_t>(n));
    if (auto r = readSamples(buffer, buffer_channel, sample_offset, samples_in_chunk, out); !r) {
      return std::unexpected(
          std::format("reading channel {} at sample {}: {}", buffer_channel, buffer.describeSample(sample_offset),
                      r.error()));
    }
    if (deflater) {
      deflater->submit(static_cast<size_t>(n));
      continue;
    }
    const hsize_t start = static_cast<hsize_t>(first);
    const hsize_t count = static_cast<hsize_t>(n);
    Handle file_space(H5Dget_space(dataset.id()), H5Sclose);
    if (!file_space.ok() ||
        H5Sselect_hyperslab(file_space.id(), H5S_SELECT_SET, &start, nullptr, &count, nullptr) < 0) {
      return fail("cannot select region to write");
    }
    Handle mem_space(H5Screate_simple(1, &count, nullptr), H5Sclose);
    if (!mem_space.ok()) return fail("cannot create memory dataspace");
    if (H5Dwrite(dataset.id(), H5T_NATIVE_UINT8, mem_space.id(), file_space.id(), H5P_DEFAULT,
                 out.data()) < 0) {
      return fail(std::format("cannot write channel {} at byte {}", name, first));
    }
  }
  if (deflater) {
    if (auto r = deflater->finish(writeDeflated); !r) return r;
  }
  return {};
}

std::expected<void, std::string> writeDecoderSetup(hid_t device_group,
                                                   std::span<const std::pair<std::string, std::string>> setup,
                                                   std::span<const std::string> presentation,
                                                   std::span<const bitoscope::capture::DecoderVersions> versions) {
  Handle group(H5Gcreate2(device_group, "decoder_setup", H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT), H5Gclose);
  if (!group.ok()) return fail("cannot create device's decoder_setup group");
  for (size_t i = 0; i < setup.size(); ++i) {
    const std::string name = std::to_string(i);
    Handle entry(H5Gcreate2(group.id(), name.c_str(), H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT), H5Gclose);
    if (!entry.ok()) return fail(std::format("cannot create decoder_setup/{}", name));
    if (auto r = writeStringAttr(entry.id(), "key", setup[i].first); !r) return r;
    if (auto r = writeStringAttr(entry.id(), "value", setup[i].second); !r) return r;
    if (i < presentation.size() && !presentation[i].empty()) {
      if (auto r = writeStringAttr(entry.id(), "presentation", presentation[i]); !r) return r;
    }
    if (i < versions.size() && !versions[i].plugin_version.empty()) {
      if (auto r = writeStringAttr(entry.id(), "plugin_version", versions[i].plugin_version); !r) return r;
    }
    if (i < versions.size() && !versions[i].decoder_version.empty()) {
      if (auto r = writeStringAttr(entry.id(), "decoder_version", versions[i].decoder_version); !r) return r;
    }
  }
  if (!group.close()) return fail("cannot close group");
  return {};
}

std::expected<void, std::string> checkChannelNameCount(const bitoscope::capture::ViewState& view,
                                                       size_t channel_count) {
  if (!view.channel_names.empty() && view.channel_names.size() != channel_count) {
    return std::unexpected(std::format(
        "view state holds {} channel name{}, but capture has {} channel{}", view.channel_names.size(),
        view.channel_names.size() == 1 ? "" : "s", channel_count, channel_count == 1 ? "" : "s"));
  }
  return {};
}

std::expected<void, std::string> writeView(hid_t group, const bitoscope::capture::ViewState& view,
                                           size_t channel_count) {
  if (auto r = checkChannelNameCount(view, channel_count); !r) return r;
  if (!view.channel_colors.empty() && view.channel_colors.size() != channel_count) {
    return std::unexpected(std::format(
        "view state names {} channel color{}, but capture has {} channel{}",
        view.channel_colors.size(), view.channel_colors.size() == 1 ? "" : "s", channel_count,
        channel_count == 1 ? "" : "s"));
  }

  Handle view_group(H5Gcreate2(group, "view", H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT), H5Gclose);
  if (!view_group.ok()) return fail("cannot create device's view group");

  // WHY: Absent, not a sentinel: 0 is a legitimate cursor sample.
  if (view.cursors) {
    const uint8_t shown = view.cursors->shown ? 1 : 0;
    if (auto r = writeScalarAttr(view_group.id(), "cursors_shown", H5T_STD_U8LE, H5T_NATIVE_UINT8,
                                 shown);
        !r) {
      return r;
    }
    if (auto r = writeScalarAttr(view_group.id(), "cursor_first", H5T_STD_U64LE,
                                 H5T_NATIVE_UINT64, view.cursors->first);
        !r) {
      return r;
    }
    if (auto r = writeScalarAttr(view_group.id(), "cursor_second", H5T_STD_U64LE,
                                 H5T_NATIVE_UINT64, view.cursors->second);
        !r) {
      return r;
    }
  }

  if (view.lane_order) {
    if (auto r = writeStringAttr(view_group.id(), "lane_order", *view.lane_order); !r) return r;
  }

  Handle markers(H5Gcreate2(view_group.id(), "markers", H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT),
                H5Gclose);
  if (!markers.ok()) return fail("cannot create view's markers group");
  for (size_t i = 0; i < view.markers.size(); ++i) {
    const std::string name = std::to_string(i);
    Handle entry(H5Gcreate2(markers.id(), name.c_str(), H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT),
                H5Gclose);
    if (!entry.ok()) return fail(std::format("cannot create view/markers/{}", name));
    if (auto r = writeScalarAttr(entry.id(), "id", H5T_STD_U64LE, H5T_NATIVE_UINT64,
                                 view.markers[i].id);
        !r) {
      return r;
    }
    if (auto r = writeScalarAttr(entry.id(), "sample", H5T_STD_U64LE, H5T_NATIVE_UINT64,
                                 view.markers[i].sample);
        !r) {
      return r;
    }
    if (auto r = writeStringAttr(entry.id(), "name", view.markers[i].name); !r) return r;
  }
  if (!markers.close()) return fail("cannot close group");

  if (!view.channel_colors.empty()) {
    Handle colors(H5Gcreate2(view_group.id(), "channel_colors", H5P_DEFAULT, H5P_DEFAULT,
                             H5P_DEFAULT),
                  H5Gclose);
    if (!colors.ok()) return fail("cannot create view's channel_colors group");
    for (size_t i = 0; i < view.channel_colors.size(); ++i) {
      const std::string name = std::to_string(i);
      Handle entry(H5Gcreate2(colors.id(), name.c_str(), H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT),
                  H5Gclose);
      if (!entry.ok()) return fail(std::format("cannot create view/channel_colors/{}", name));
      if (auto r = writeStringAttr(entry.id(), "color", view.channel_colors[i]); !r) return r;
    }
    if (!colors.close()) return fail("cannot close group");
  }

  if (!view.channel_names.empty()) {
    Handle names(H5Gcreate2(view_group.id(), "channel_names", H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT),
                 H5Gclose);
    if (!names.ok()) return fail("cannot create view's channel_names group");
    for (size_t i = 0; i < view.channel_names.size(); ++i) {
      const std::string name = std::to_string(i);
      Handle entry(H5Gcreate2(names.id(), name.c_str(), H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT),
                  H5Gclose);
      if (!entry.ok()) return fail(std::format("cannot create view/channel_names/{}", name));
      if (auto r = writeStringAttr(entry.id(), "name", view.channel_names[i]); !r) return r;
    }
    if (!names.close()) return fail("cannot close group");
  }

  if (!view_group.close()) return fail("cannot close group");
  return {};
}

std::expected<void, std::string> writeDevice(hid_t group, const RawCaptureBuffer& buffer,
                                             const CaptureMetadata& metadata, int64_t alignment_offset_ps,
                                             const Hdf5WriterOptions& options) {
  const bitoscope::capture::CapturePlan& plan = buffer.plan();
  const uint64_t extent_first_sample = metadata.extent.first_sample;
  const uint64_t stored_samples = metadata.extent.count;
  const uint64_t stored_bytes = (stored_samples + 7) / 8;

  if (auto r = writeStringAttr(group, "device", metadata.device); !r) return r;
  if (auto r = writeScalarAttr(group, "alignment_offset_ps", H5T_STD_I64LE, H5T_NATIVE_INT64, alignment_offset_ps);
      !r) {
    return r;
  }
  if (auto r = writeScalarAttr(group, "sample_rate_milli_hz", H5T_STD_U64LE,
                               H5T_NATIVE_UINT64, plan.sampleRateMilliHz());
      !r) {
    return r;
  }
  if (auto r = writeScalarAttr(group, "sample_count", H5T_STD_U64LE, H5T_NATIVE_UINT64,
                               stored_samples);
      !r) {
    return r;
  }
  if (auto r = writeScalarAttr(group, "channel_count", H5T_STD_U32LE, H5T_NATIVE_UINT32,
                               plan.channels());
      !r) {
    return r;
  }
  if (auto r = writeScalarAttr(group, "capture_id", H5T_STD_U64LE, H5T_NATIVE_UINT64,
                               metadata.capture_id);
      !r) {
    return r;
  }
  // WHY: Omitted, not sentinel-valued: sample index 0 is a legitimate origin or trigger position.
  if (metadata.origin_sample_index) {
    if (auto r = writeScalarAttr(group, "origin_sample_index", H5T_STD_U64LE,
                                 H5T_NATIVE_UINT64, *metadata.origin_sample_index);
        !r) {
      return r;
    }
  }
  if (metadata.trigger_sample_index) {
    if (auto r = writeScalarAttr(group, "trigger_sample_index", H5T_STD_U64LE,
                                 H5T_NATIVE_UINT64, *metadata.trigger_sample_index);
        !r) {
      return r;
    }
  }

  if (auto r = writeScalarAttr(group, "requested_sample_count", H5T_STD_U64LE,
                               H5T_NATIVE_UINT64, metadata.requested_samples);
      !r) {
    return r;
  }
  if (metadata.requested_pre_trigger_samples && metadata.requested_post_trigger_samples) {
    const uint64_t pre = *metadata.requested_pre_trigger_samples;
    if (auto r = writeScalarAttr(group, "requested_pre_trigger_sample_count", H5T_STD_U64LE,
                                 H5T_NATIVE_UINT64, pre);
        !r) {
      return r;
    }
    if (auto r = writeScalarAttr(group, "requested_post_trigger_sample_count", H5T_STD_U64LE,
                                 H5T_NATIVE_UINT64, *metadata.requested_post_trigger_samples);
        !r) {
      return r;
    }
    if (auto r = writeScalarAttr(group, "post_trigger_sample_count", H5T_STD_U64LE,
                                 H5T_NATIVE_UINT64, stored_samples - pre);
        !r) {
      return r;
    }
  }
  if (metadata.memory_shortfall) {
    if (auto r = writeStringAttr(group, "short_reason", shortReasonText(metadata.memory_shortfall->reason)); !r) {
      return r;
    }
    if (const auto full = metadata.memory_shortfall->memory_full_device) {
      if (auto r = writeScalarAttr(group, "memory_full_device", H5T_STD_U32LE, H5T_NATIVE_UINT32, *full); !r) return r;
    }
  }

  Handle channels(H5Gcreate2(group, "channels", H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT),
                  H5Gclose);
  if (!channels.ok()) return fail("cannot create device's channels group");
  for (uint32_t c = 0; c < plan.channels(); ++c) {
    if (auto r = writeChannel(channels.id(), buffer, c, metadata.channels[c], extent_first_sample,
                              stored_samples, stored_bytes, options);
        !r) {
      return r;
    }
    if (options.after_channel) {
      if (auto r = options.after_channel(c); !r) return r;
    }
  }

  if (auto r = writeDecoderSetup(group, metadata.decoder_setup, metadata.decoder_presentation,
                                 metadata.decoder_versions);
      !r) {
    return r;
  }

  Handle props(H5Gcreate2(group, "device_properties", H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT),
               H5Gclose);
  if (!props.ok()) return fail("cannot create device's device_properties group");
  for (size_t i = 0; i < metadata.device_properties.size(); ++i) {
    const std::string name = std::to_string(i);
    Handle entry(H5Gcreate2(props.id(), name.c_str(), H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT),
                 H5Gclose);
    if (!entry.ok()) return fail(std::format("cannot create device_properties/{}", name));
    const bitoscope::capture::SavedProperty& p = metadata.device_properties[i];
    if (auto r = writeStringAttr(entry.id(), "facet", p.facet); !r) return r;
    if (auto r = writeStringAttr(entry.id(), "id", p.id); !r) return r;
    if (auto r = writeStringAttr(entry.id(), "name", p.name); !r) return r;
    if (auto r = writeStringAttr(entry.id(), "formatted", p.formatted); !r) return r;
    if (const auto* integer = std::get_if<bitoscope::capture::SavedIntegerValue>(&p.value)) {
      if (auto r = writeStringAttr(entry.id(), "kind", "integer"); !r) return r;
      if (auto r = writeScalarAttr(entry.id(), "value", H5T_STD_I64LE, H5T_NATIVE_INT64,
                                   integer->value);
          !r) {
        return r;
      }
      if (auto r = writeScalarAttr(entry.id(), "digits", H5T_STD_U32LE, H5T_NATIVE_UINT32,
                                   integer->digits);
          !r) {
        return r;
      }
      if (!integer->unit.empty()) {
        if (auto r = writeStringAttr(entry.id(), "unit", integer->unit); !r) return r;
      }
    } else if (const auto* boolean = std::get_if<bool>(&p.value)) {
      if (auto r = writeStringAttr(entry.id(), "kind", "boolean"); !r) return r;
      const uint8_t v = *boolean ? 1 : 0;
      if (auto r = writeScalarAttr(entry.id(), "value", H5T_STD_U8LE, H5T_NATIVE_UINT8, v); !r) {
        return r;
      }
    } else if (const auto* enumeration = std::get_if<bitoscope::capture::SavedEnumValue>(&p.value)) {
      if (auto r = writeStringAttr(entry.id(), "kind", "enumeration"); !r) return r;
      if (enumeration->name) {
        if (auto r = writeStringAttr(entry.id(), "enum_name", *enumeration->name); !r) return r;
      }
      if (enumeration->number) {
        if (auto r = writeScalarAttr(entry.id(), "enum_number", H5T_STD_I64LE, H5T_NATIVE_INT64,
                                     *enumeration->number);
            !r) {
          return r;
        }
        if (auto r = writeScalarAttr(entry.id(), "digits", H5T_STD_U32LE, H5T_NATIVE_UINT32,
                                     enumeration->digits);
            !r) {
          return r;
        }
        if (!enumeration->unit.empty()) {
          if (auto r = writeStringAttr(entry.id(), "unit", enumeration->unit); !r) return r;
        }
      }
    }
  }

  if (metadata.view) {
    if (auto r = writeView(group, *metadata.view, metadata.channels.size()); !r) return r;
  }

  if (!channels.close() || !props.close()) return fail("cannot close group");
  return {};
}

std::expected<void, std::string> writeSessionFile(const std::filesystem::path& temp,
                                                  std::span<const SessionDeviceCapture> devices,
                                                  const SessionInfo& session, const Hdf5WriterOptions& options) {
  const std::string native = utf8Path(temp);
  Handle file(H5Fcreate(native.c_str(), H5F_ACC_EXCL, H5P_DEFAULT, H5P_DEFAULT), H5Fclose);
  if (!file.ok()) return fail(std::format("cannot create {}", native));
  if (auto r = writeScalarAttr(file.id(), "format_version", H5T_STD_U32LE, H5T_NATIVE_UINT32, kFormatVersion); !r) {
    return r;
  }
  if (auto r = writeStringAttr(file.id(), "bit_order", kBitOrder); !r) return r;
  const auto count = static_cast<uint32_t>(devices.size());
  if (auto r = writeScalarAttr(file.id(), "device_count", H5T_STD_U32LE, H5T_NATIVE_UINT32, count); !r) return r;
  const uint8_t locked = session.alignment_locked ? 1 : 0;
  if (auto r = writeScalarAttr(file.id(), "alignment_locked", H5T_STD_U8LE, H5T_NATIVE_UINT8, locked); !r) return r;
  if (const auto reason = sessionShortReason(devices)) {
    if (auto r = writeStringAttr(file.id(), "short_reason", shortReasonText(*reason)); !r) return r;
  }
  Handle all(H5Gcreate2(file.id(), "/devices", H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT), H5Gclose);
  if (!all.ok()) return fail("cannot create /devices group");
  for (uint32_t n = 0; n < count; ++n) {
    const std::string name = std::to_string(n);
    Handle group(H5Gcreate2(all.id(), name.c_str(), H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT), H5Gclose);
    if (!group.ok()) return fail(std::format("cannot create /devices/{}", name));
    const SessionDeviceCapture& device = devices[n];
    if (auto r = writeDevice(group.id(), device.buffer, device.metadata, device.alignment_offset_ps, options); !r) {
      return r;
    }
    if (!group.close()) return fail("cannot close group");
  }
  if (!all.close()) return fail("cannot close group");
  if (!file.close()) return fail("cannot close file (is disk full?)");
  return {};
}

}

std::expected<void, std::string> Hdf5CaptureWriter::write(const RawCaptureBuffer& buffer,
                                                          const CaptureMetadata& metadata) {
  const SessionDeviceCapture one{buffer, metadata, 0};
  return writeSession(std::span<const SessionDeviceCapture>(&one, 1), SessionInfo{});
}

std::expected<void, std::string> Hdf5CaptureWriter::writeSession(std::span<const SessionDeviceCapture> devices,
                                                                 const SessionInfo& session) {
  const std::lock_guard<std::recursive_mutex> lock(libraryMutex());
  if (devices.empty()) return std::unexpected("session file needs at least one device");
  if (options_.gzip_level > kMaxGzipLevel) {
    return std::unexpected(
        std::format("gzip level {} is not in 0-{}", options_.gzip_level, kMaxGzipLevel));
  }
  if (options_.chunk_bytes == 0) return std::unexpected("HDF5 chunk size must be at least 1 byte");

  for (size_t n = 0; n < devices.size(); ++n) {
    if (auto ok = bitoscope::capture::checkSavable(devices[n].buffer, devices[n].metadata); !ok) {
      return std::unexpected(devices.size() == 1 ? ok.error() : std::format("device {}: {}", n, ok.error()));
    }
  }

  std::error_code ec;
  const std::filesystem::path directory =
      path_.has_parent_path() ? path_.parent_path() : std::filesystem::path(".");
  if (!std::filesystem::is_directory(directory, ec)) {
    return std::unexpected(
        std::format("directory {} does not exist", utf8Path(directory)));
  }
  if (std::filesystem::exists(path_, ec)) {
    return std::unexpected(std::format(
        "{} already exists; remove it or choose another name (nothing was saved)", utf8Path(path_)));
  }
  std::filesystem::path temp = path_;
  temp += ".tmp";
  if (std::filesystem::exists(temp, ec)) {
    return std::unexpected(std::format(
        "{} already exists (leftover of earlier save?); remove it first", utf8Path(temp)));
  }

  H5Eset_auto2(H5E_DEFAULT, nullptr, nullptr);

  std::expected<void, std::string> written;
  try {
    written = writeSessionFile(temp, devices, session, options_);
  } catch (const std::bad_alloc&) {
    written = std::unexpected("out of memory while writing capture");
  }
  if (!written) {
    std::filesystem::remove(temp, ec);
    return written;
  }

  std::filesystem::rename(temp, path_, ec);
  if (ec) {
    const std::string message = ec.message();
    std::filesystem::remove(temp, ec);
    return std::unexpected(
        std::format("cannot move finished file to {}: {}", utf8Path(path_), message));
  }
  return {};
}

namespace {

std::expected<Handle, std::string> openDeviceGroup(hid_t file, const std::string& native,
                                                   uint32_t device_index) {
  const auto version = readU32LikeAttr<uint32_t>(file, "format_version", H5T_NATIVE_UINT32);
  if (!version) {
    return fail(std::format("'{}' is not bitoscope session file: no format_version attribute", native));
  }
  if (*version != kFormatVersion) {
    return fail(std::format("'{}' is format version {}, which this build does not support (only {})", native,
                            *version, kFormatVersion));
  }
  if (H5Lexists(file, "devices", H5P_DEFAULT) <= 0) {
    return fail(std::format("'{}' is not bitoscope session file: it has no /devices group", native));
  }
  Handle devices(H5Gopen2(file, "devices", H5P_DEFAULT), H5Gclose);
  if (!devices.ok()) return fail(std::format("cannot open /devices group of '{}'", native));
  const std::string name = std::to_string(device_index);
  if (H5Lexists(devices.id(), name.c_str(), H5P_DEFAULT) <= 0) {
    const auto count = readU32LikeAttr<uint32_t>(file, "device_count", H5T_NATIVE_UINT32);
    return fail(std::format("device {} is out of range: '{}' has {} device{}", device_index, native,
                            count.value_or(0), count.value_or(0) == 1 ? "" : "s"));
  }
  Handle group(H5Gopen2(devices.id(), name.c_str(), H5P_DEFAULT), H5Gclose);
  if (!group.ok()) return fail(std::format("cannot open device {} of '{}'", device_index, native));
  return group;
}

herr_t visitObject(hid_t, const char*, const H5O_info2_t*, void*) { return 0; }

// WHY: Any header failure below an existing /view is damage: the later unlink loads the same headers.
bool viewGroupReadable(hid_t device) {
  Handle view(H5Gopen2(device, "view", H5P_DEFAULT), H5Gclose);
  return view.ok() &&
         H5Ovisit3(view.id(), H5_INDEX_NAME, H5_ITER_NATIVE, visitObject, nullptr, H5O_INFO_BASIC) >= 0;
}

}

std::expected<void, std::string> updateSessionView(const std::filesystem::path& path, uint32_t device_index,
                                                    const bitoscope::capture::ViewState& view) {
  const std::lock_guard<std::recursive_mutex> lock(libraryMutex());
  H5Eset_auto2(H5E_DEFAULT, nullptr, nullptr);
  const std::string native = utf8Path(path);

  // WHY: Validate read-only first, so a refused update leaves the file byte-identical.
  uint32_t channel_count = 0;
  {
    Handle file(H5Fopen(native.c_str(), H5F_ACC_RDONLY, H5P_DEFAULT), H5Fclose);
    if (!file.ok()) {
      return fail(std::format("cannot open '{}': not readable HDF5 file", native));
    }
    auto device = openDeviceGroup(file.id(), native, device_index);
    if (!device) return std::unexpected(device.error());
    const auto count = readU32LikeAttr<uint32_t>(device->id(), "channel_count", H5T_NATIVE_UINT32);
    if (!count) {
      return fail(std::format("device {} of '{}' has no channel_count attribute", device_index, native));
    }
    channel_count = *count;
    if (H5Lexists(device->id(), "view", H5P_DEFAULT) > 0 && !viewGroupReadable(device->id())) {
      return fail(std::format("view of device {} in '{}' is damaged: use Save as to write new file",
                              device_index, native));
    }
  }
  if (!view.channel_colors.empty() && view.channel_colors.size() != channel_count) {
    return std::unexpected(std::format(
        "view state names {} channel color{}, but capture has {} channel{}",
        view.channel_colors.size(), view.channel_colors.size() == 1 ? "" : "s", channel_count,
        channel_count == 1 ? "" : "s"));
  }
  if (auto r = checkChannelNameCount(view, channel_count); !r) return r;

  // WHY: Unlink the old /view, never rewrite the file: the file never shrinks, accepted to avoid a full rewrite.
  Handle file(H5Fopen(native.c_str(), H5F_ACC_RDWR, H5P_DEFAULT), H5Fclose);
  if (!file.ok()) {
    return fail(std::format("cannot reopen '{}' for writing", native));
  }
  auto device = openDeviceGroup(file.id(), native, device_index);
  if (!device) return std::unexpected(device.error());
  if (H5Lexists(device->id(), "view", H5P_DEFAULT) > 0) {
    if (H5Ldelete(device->id(), "view", H5P_DEFAULT) < 0) {
      return fail(std::format("cannot remove old view group of device {} of '{}'", device_index, native));
    }
  }
  if (auto r = writeView(device->id(), view, channel_count); !r) return r;
  if (!device->close()) return fail("cannot close group");
  if (!file.close()) return fail(std::format("cannot close '{}' (is disk full?)", native));
  return {};
}

std::expected<void, std::string> updateSessionDecoders(
    const std::filesystem::path& path, uint32_t device_index,
    std::span<const std::pair<std::string, std::string>> decoder_setup, const Hdf5WriterOptions& options) {
  return updateSessionDecoders(path, device_index, decoder_setup, {}, options);
}

std::expected<void, std::string> updateSessionDecoders(
    const std::filesystem::path& path, uint32_t device_index,
    std::span<const std::pair<std::string, std::string>> decoder_setup,
    std::span<const std::string> decoder_presentation, const Hdf5WriterOptions& options) {
  return updateSessionDecoders(path, device_index, decoder_setup, decoder_presentation, {}, options);
}

std::expected<void, std::string> updateSessionDecoders(
    const std::filesystem::path& path, uint32_t device_index,
    std::span<const std::pair<std::string, std::string>> decoder_setup,
    std::span<const std::string> decoder_presentation,
    std::span<const bitoscope::capture::DecoderVersions> decoder_versions,
    const Hdf5WriterOptions& options) {
  const std::lock_guard<std::recursive_mutex> lock(libraryMutex());
  H5Eset_auto2(H5E_DEFAULT, nullptr, nullptr);
  const std::string native = utf8Path(path);
  if (options.gzip_level > kMaxGzipLevel) {
    return std::unexpected(std::format("gzip level {} is not in 0-{}", options.gzip_level, kMaxGzipLevel));
  }
  if (options.chunk_bytes == 0) return std::unexpected("HDF5 chunk size must be at least 1 byte");

  {
    Handle file(H5Fopen(native.c_str(), H5F_ACC_RDONLY, H5P_DEFAULT), H5Fclose);
    if (!file.ok()) {
      return fail(std::format("cannot open '{}': not readable HDF5 file", native));
    }
    auto device = openDeviceGroup(file.id(), native, device_index);
    if (!device) return std::unexpected(device.error());
    if (!decoder_presentation.empty() && decoder_presentation.size() != decoder_setup.size()) {
      return std::unexpected(std::format(
          "capture names {} decoder_setup entries but {} decoder presentations: one per entry, or none at all",
          decoder_setup.size(), decoder_presentation.size()));
    }
    if (!decoder_versions.empty() && decoder_versions.size() != decoder_setup.size()) {
      return std::unexpected(std::format(
          "capture names {} decoder_setup entries but {} decoder versions: one per entry, or none at all",
          decoder_setup.size(), decoder_versions.size()));
    }
  }

  Handle file(H5Fopen(native.c_str(), H5F_ACC_RDWR, H5P_DEFAULT), H5Fclose);
  if (!file.ok()) {
    return fail(std::format("cannot reopen '{}' for writing", native));
  }
  auto device = openDeviceGroup(file.id(), native, device_index);
  if (!device) return std::unexpected(device.error());
  if (H5Lexists(device->id(), "decoder_setup", H5P_DEFAULT) > 0) {
    if (H5Ldelete(device->id(), "decoder_setup", H5P_DEFAULT) < 0) {
      return fail(std::format("cannot remove old decoder_setup group of device {} of '{}'", device_index, native));
    }
  }
  try {
    if (auto r = writeDecoderSetup(device->id(), decoder_setup, decoder_presentation, decoder_versions); !r) return r;
  } catch (const std::bad_alloc&) {
    return std::unexpected("out of memory while writing decoder setup");
  }
  if (!device->close()) return fail("cannot close group");
  if (!file.close()) return fail(std::format("cannot close '{}' (is disk full?)", native));
  return {};
}

}
