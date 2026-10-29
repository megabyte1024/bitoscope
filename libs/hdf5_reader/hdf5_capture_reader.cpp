/*
 * SPDX-License-Identifier: MIT
 */

#include "hdf5_capture_reader.h"
#include "hdf5_lock.h"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <format>
#include <memory>
#include <new>
#include <thread>
#include <variant>
#include <vector>

#include <hdf5.h>
#include <zlib.h>

#include "arena_growth.h"
#include "chunk_inflate.h"
#include "pack_pool.h"
#include "ring_config.h"
#include "system_memory_probe.h"

namespace bitoscope::hdf5 {

namespace {

using bitoscope::capture::AllocatorSettings;
using bitoscope::capture::BlockSource;
using bitoscope::capture::CapturePlan;
using bitoscope::capture::kMaxChannels;
using bitoscope::capture::makeCapturePlan;
using bitoscope::capture::MemoryProbe;
using bitoscope::capture::RawCaptureBuffer;
using bitoscope::capture::Reservation;
using bitoscope::capture::SavedChannel;
using bitoscope::capture::SavedEnumValue;
using bitoscope::capture::SavedIntegerValue;
using bitoscope::capture::SavedProperty;
using bitoscope::host::WindowInfo;
using bitoscope::host::WindowPool;

constexpr uint32_t kFormatVersion = 4;

std::string hdf5Text(std::string_view what) { return std::format("HDF5: {}", what); }

std::unexpected<std::string> fail(std::string_view what) { return std::unexpected(hdf5Text(what)); }

// WHY: Use UTF-8, not path::string(): that narrows through the ANSI code page on Windows.
std::string utf8Path(const std::filesystem::path& path) {
  const std::u8string text = path.u8string();
  return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}

class Handle {
 public:
  using Closer = herr_t (*)(hid_t);
  Handle() = default;
  Handle(hid_t id, Closer closer) : id_(id), closer_(closer) {}
  Handle(const Handle&) = delete;
  Handle& operator=(const Handle&) = delete;
  Handle(Handle&& other) noexcept : id_(other.id_), closer_(other.closer_) { other.id_ = -1; }
  Handle& operator=(Handle&& other) noexcept {
    if (this != &other) {
      if (id_ >= 0 && closer_ != nullptr) closer_(id_);
      id_ = other.id_;
      closer_ = other.closer_;
      other.id_ = -1;
    }
    return *this;
  }
  ~Handle() {
    if (id_ >= 0 && closer_ != nullptr) closer_(id_);
  }
  hid_t id() const { return id_; }
  bool ok() const { return id_ >= 0; }

 private:
  hid_t id_ = -1;
  Closer closer_ = nullptr;
};

bool attrExists(hid_t obj, const char* name) {
  const htri_t r = H5Aexists(obj, name);
  return r > 0;
}

template <typename T>
std::optional<T> readScalarAttr(hid_t obj, const char* name, hid_t mem_type) {
  if (!attrExists(obj, name)) return std::nullopt;
  Handle attr(H5Aopen(obj, name, H5P_DEFAULT), H5Aclose);
  if (!attr.ok()) return std::nullopt;
  T value{};
  if (H5Aread(attr.id(), mem_type, &value) < 0) return std::nullopt;
  return value;
}

template <typename T>
std::expected<T, std::string> requireScalarAttr(hid_t obj, const char* name, hid_t mem_type) {
  auto v = readScalarAttr<T>(obj, name, mem_type);
  if (!v) return fail(std::format("missing or unreadable attribute '{}'", name));
  return *v;
}

std::optional<std::string> readOptionalStringAttr(hid_t obj, const char* name) {
  if (!attrExists(obj, name)) return std::nullopt;
  Handle attr(H5Aopen(obj, name, H5P_DEFAULT), H5Aclose);
  if (!attr.ok()) return std::nullopt;
  Handle type(H5Aget_type(attr.id()), H5Tclose);
  if (!type.ok()) return std::nullopt;
  const size_t size = H5Tget_size(type.id());
  if (size == 0) return std::string();
  std::string text(size, '\0');
  if (H5Aread(attr.id(), type.id(), text.data()) < 0) return std::nullopt;
  const size_t end = text.find('\0');
  if (end != std::string::npos) text.resize(end);
  return text;
}

std::expected<std::string, std::string> requireStringAttr(hid_t obj, const char* name) {
  auto v = readOptionalStringAttr(obj, name);
  if (!v) return fail(std::format("missing or unreadable attribute '{}'", name));
  return *v;
}

std::expected<std::string, std::string> readTextAttrChecked(hid_t obj, const char* name) {
  Handle attr(H5Aopen(obj, name, H5P_DEFAULT), H5Aclose);
  if (!attr.ok()) return fail(std::format("cannot open attribute '{}'", name));
  Handle type(H5Aget_type(attr.id()), H5Tclose);
  if (!type.ok()) return fail(std::format("cannot read type of attribute '{}'", name));
  if (H5Tget_class(type.id()) != H5T_STRING || H5Tis_variable_str(type.id()) > 0) {
    return fail(std::format("attribute '{}' is not fixed-length text", name));
  }
  auto text = readOptionalStringAttr(obj, name);
  if (!text) return fail(std::format("cannot read attribute '{}'", name));
  return *text;
}

struct ChannelEntry {
  uint32_t index = 0;
  std::string name;
  Handle dataset;
};

// WHY: Iterate members: channel indexes need not be contiguous, so probing names 0..N-1 would miss channels.
std::expected<std::vector<ChannelEntry>, std::string> discoverChannels(hid_t channels_group,
                                                                       uint32_t expected_count,
                                                                       uint64_t expected_bytes) {
  H5G_info_t info{};
  if (H5Gget_info(channels_group, &info) < 0) return fail("cannot read channels group");
  if (info.nlinks != expected_count) {
    return fail(std::format("channels group holds {} datasets, channel_count says {}",
                            static_cast<uint64_t>(info.nlinks), expected_count));
  }

  std::vector<ChannelEntry> out;
  try {
    out.reserve(expected_count);
  } catch (const std::bad_alloc&) {
    return fail("out of memory listing channels");
  }
  for (hsize_t i = 0; i < info.nlinks; ++i) {
    char name_buf[64] = {};
    const ssize_t len = H5Lget_name_by_idx(channels_group, ".", H5_INDEX_NAME, H5_ITER_INC, i,
                                           name_buf, sizeof(name_buf), H5P_DEFAULT);
    if (len < 0 || static_cast<size_t>(len) >= sizeof(name_buf)) {
      return fail("cannot read channel dataset's name");
    }
    Handle dataset(H5Dopen2(channels_group, name_buf, H5P_DEFAULT), H5Dclose);
    if (!dataset.ok()) return fail(std::format("cannot open channel dataset '{}'", name_buf));

    Handle space(H5Dget_space(dataset.id()), H5Sclose);
    if (!space.ok() || H5Sget_simple_extent_ndims(space.id()) != 1) {
      return fail(std::format("channel dataset '{}' is not one-dimensional", name_buf));
    }
    hsize_t dim = 0;
    if (H5Sget_simple_extent_dims(space.id(), &dim, nullptr) < 0) {
      return fail(std::format("cannot read size of channel dataset '{}'", name_buf));
    }
    if (dim != expected_bytes) {
      return fail(std::format(
          "channel dataset '{}' holds {} bytes, but capture's sample_count needs {}: file "
          "is truncated or corrupt",
          name_buf, static_cast<uint64_t>(dim), expected_bytes));
    }

    auto index = requireScalarAttr<uint32_t>(dataset.id(), "index", H5T_NATIVE_UINT32);
    if (!index) return std::unexpected(std::format("channel '{}': {}", name_buf, index.error()));
    auto ch_name = requireStringAttr(dataset.id(), "name");
    if (!ch_name) return std::unexpected(std::format("channel '{}': {}", name_buf, ch_name.error()));

    ChannelEntry entry;
    entry.index = *index;
    entry.name = *ch_name;
    entry.dataset = std::move(dataset);
    out.push_back(std::move(entry));
  }
  std::sort(out.begin(), out.end(),
           [](const ChannelEntry& a, const ChannelEntry& b) { return a.index < b.index; });
  for (size_t i = 1; i < out.size(); ++i) {
    if (out[i].index == out[i - 1].index) {
      return fail(std::format("two channel datasets share index {}", out[i].index));
    }
  }
  return out;
}

std::expected<std::vector<std::pair<std::string, std::string>>, std::string> readDecoderSetup(
    hid_t device_group, std::vector<std::string>& presentation,
    std::vector<bitoscope::capture::DecoderVersions>& versions) {
  presentation.clear();
  versions.clear();
  std::vector<std::pair<std::string, std::string>> out;
  if (!H5Lexists(device_group, "decoder_setup", H5P_DEFAULT)) return out;
  Handle group(H5Gopen2(device_group, "decoder_setup", H5P_DEFAULT), H5Gclose);
  if (!group.ok()) return fail("cannot open decoder_setup");
  for (uint32_t i = 0;; ++i) {
    const std::string name = std::to_string(i);
    if (!H5Lexists(group.id(), name.c_str(), H5P_DEFAULT)) break;
    Handle entry(H5Gopen2(group.id(), name.c_str(), H5P_DEFAULT), H5Gclose);
    if (!entry.ok()) return fail(std::format("cannot open decoder_setup/{}", i));
    auto key = requireStringAttr(entry.id(), "key");
    auto value = requireStringAttr(entry.id(), "value");
    if (!key) return std::unexpected(std::format("decoder_setup/{}: {}", i, key.error()));
    if (!value) return std::unexpected(std::format("decoder_setup/{}: {}", i, value.error()));
    try {
      out.emplace_back(*key, *value);
      presentation.push_back(readOptionalStringAttr(entry.id(), "presentation").value_or(""));
      versions.push_back({readOptionalStringAttr(entry.id(), "plugin_version").value_or(""),
                          readOptionalStringAttr(entry.id(), "decoder_version").value_or("")});
    } catch (const std::bad_alloc&) {
      return fail("out of memory reading decoder_setup");
    }
  }
  if (std::all_of(presentation.begin(), presentation.end(), [](const std::string& p) { return p.empty(); })) {
    presentation.clear();
  }
  if (std::all_of(versions.begin(), versions.end(),
                  [](const bitoscope::capture::DecoderVersions& v) { return v == bitoscope::capture::DecoderVersions{}; })) {
    versions.clear();
  }
  return out;
}

std::expected<std::vector<SavedProperty>, std::string> readDeviceProperties(hid_t device_group) {
  std::vector<SavedProperty> out;
  if (!H5Lexists(device_group, "device_properties", H5P_DEFAULT)) return out;
  Handle group(H5Gopen2(device_group, "device_properties", H5P_DEFAULT), H5Gclose);
  if (!group.ok()) return fail("cannot open device_properties");
  for (uint32_t i = 0;; ++i) {
    const std::string name = std::to_string(i);
    if (!H5Lexists(group.id(), name.c_str(), H5P_DEFAULT)) break;
    Handle entry(H5Gopen2(group.id(), name.c_str(), H5P_DEFAULT), H5Gclose);
    if (!entry.ok()) return fail(std::format("cannot open device_properties/{}", i));

    SavedProperty p;
    auto facet = requireStringAttr(entry.id(), "facet");
    auto id = requireStringAttr(entry.id(), "id");
    auto pname = requireStringAttr(entry.id(), "name");
    auto formatted = requireStringAttr(entry.id(), "formatted");
    auto kind = requireStringAttr(entry.id(), "kind");
    if (!facet || !id || !pname || !formatted || !kind) {
      return std::unexpected(std::format("device_properties/{}: {}", i,
                                         (!facet ? facet.error()
                                                 : !id ? id.error()
                                                 : !pname ? pname.error()
                                                 : !formatted ? formatted.error()
                                                              : kind.error())));
    }
    p.facet = *facet;
    p.id = *id;
    p.name = *pname;
    p.formatted = *formatted;

    if (*kind == "integer") {
      auto value = requireScalarAttr<int64_t>(entry.id(), "value", H5T_NATIVE_INT64);
      auto digits = requireScalarAttr<uint32_t>(entry.id(), "digits", H5T_NATIVE_UINT32);
      if (!value || !digits) {
        return fail(std::format("device_properties/{} (integer): missing value or digits", i));
      }
      SavedIntegerValue v;
      v.value = *value;
      v.digits = *digits;
      v.unit = readOptionalStringAttr(entry.id(), "unit").value_or("");
      p.value = v;
    } else if (*kind == "boolean") {
      auto value = requireScalarAttr<uint8_t>(entry.id(), "value", H5T_NATIVE_UINT8);
      if (!value) return fail(std::format("device_properties/{} (boolean): missing value", i));
      p.value = (*value != 0);
    } else if (*kind == "enumeration") {
      const bool has_name = attrExists(entry.id(), "enum_name");
      const bool has_number = attrExists(entry.id(), "enum_number");
      if (has_name == has_number) {
        return fail(std::format(
            "device_properties/{} (enumeration): must have exactly one of enum_name and "
            "enum_number",
            i));
      }
      SavedEnumValue v;
      if (has_name) {
        auto text = requireStringAttr(entry.id(), "enum_name");
        if (!text) return fail(std::format("device_properties/{}: {}", i, text.error()));
        v.name = *text;
      } else {
        auto number = requireScalarAttr<int64_t>(entry.id(), "enum_number", H5T_NATIVE_INT64);
        auto digits = requireScalarAttr<uint32_t>(entry.id(), "digits", H5T_NATIVE_UINT32);
        if (!number || !digits) {
          return fail(std::format("device_properties/{} (enumeration): missing digits", i));
        }
        v.number = *number;
        v.digits = *digits;
        v.unit = readOptionalStringAttr(entry.id(), "unit").value_or("");
      }
      p.value = v;
    } else {
      return fail(std::format("device_properties/{}: unknown kind '{}'", i, *kind));
    }
    try {
      out.push_back(std::move(p));
    } catch (const std::bad_alloc&) {
      return fail("out of memory reading device_properties");
    }
  }
  return out;
}

std::expected<std::vector<std::string>, std::string> readChannelNames(hid_t view_group, uint32_t channel_count) {
  Handle names(H5Gopen2(view_group, "channel_names", H5P_DEFAULT), H5Gclose);
  if (!names.ok()) return fail("cannot open view/channel_names");
  std::vector<std::string> out;
  for (uint32_t i = 0;; ++i) {
    const std::string entry_name = std::to_string(i);
    if (!H5Lexists(names.id(), entry_name.c_str(), H5P_DEFAULT)) break;
    Handle entry(H5Gopen2(names.id(), entry_name.c_str(), H5P_DEFAULT), H5Gclose);
    if (!entry.ok()) return fail(std::format("cannot open view/channel_names/{}", i));
    auto name = readTextAttrChecked(entry.id(), "name");
    if (!name) return fail(std::format("view/channel_names/{}: {}", i, name.error()));
    try {
      out.push_back(std::move(*name));
    } catch (const std::bad_alloc&) {
      return fail("out of memory reading view/channel_names");
    }
  }
  if (out.size() != channel_count) {
    return fail(std::format("view/channel_names holds {} channel name{}, capture has {} channel{}", out.size(),
                            out.size() == 1 ? "" : "s", channel_count, channel_count == 1 ? "" : "s"));
  }
  return out;
}

std::expected<bitoscope::capture::ViewState, std::string> readViewInner(hid_t device_group,
                                                                        uint32_t channel_count,
                                                                        std::vector<std::string>& warnings) {
  bitoscope::capture::ViewState view;
  Handle group(H5Gopen2(device_group, "view", H5P_DEFAULT), H5Gclose);
  if (!group.ok()) return fail("cannot open view group");

  const bool has_shown = attrExists(group.id(), "cursors_shown");
  const bool has_first = attrExists(group.id(), "cursor_first");
  const bool has_second = attrExists(group.id(), "cursor_second");
  if (has_shown || has_first || has_second) {
    if (!has_shown || !has_first || !has_second) {
      return fail("view's cursor pair has only some of cursors_shown/cursor_first/cursor_second");
    }
    auto shown = requireScalarAttr<uint8_t>(group.id(), "cursors_shown", H5T_NATIVE_UINT8);
    auto first = requireScalarAttr<uint64_t>(group.id(), "cursor_first", H5T_NATIVE_UINT64);
    auto second = requireScalarAttr<uint64_t>(group.id(), "cursor_second", H5T_NATIVE_UINT64);
    if (!shown || !first || !second) return fail("cannot read view's cursor pair");
    bitoscope::capture::SavedCursorPair pair;
    pair.first = *first;
    pair.second = *second;
    pair.shown = *shown != 0;
    view.cursors = pair;
  }

  if (attrExists(group.id(), "lane_order")) {
    auto text = readTextAttrChecked(group.id(), "lane_order");
    if (text) {
      view.lane_order = std::move(*text);
    } else {
      warnings.push_back(std::format("saved lane order is malformed and was dropped: view/lane_order: {}", text.error()));
    }
  }

  if (H5Lexists(group.id(), "markers", H5P_DEFAULT)) {
    Handle markers(H5Gopen2(group.id(), "markers", H5P_DEFAULT), H5Gclose);
    if (!markers.ok()) return fail("cannot open view/markers");
    for (uint32_t i = 0;; ++i) {
      const std::string name = std::to_string(i);
      if (!H5Lexists(markers.id(), name.c_str(), H5P_DEFAULT)) break;
      Handle entry(H5Gopen2(markers.id(), name.c_str(), H5P_DEFAULT), H5Gclose);
      if (!entry.ok()) return fail(std::format("cannot open view/markers/{}", i));
      auto id = requireScalarAttr<uint64_t>(entry.id(), "id", H5T_NATIVE_UINT64);
      auto sample = requireScalarAttr<uint64_t>(entry.id(), "sample", H5T_NATIVE_UINT64);
      auto mname = requireStringAttr(entry.id(), "name");
      if (!id) return fail(std::format("view/markers/{}: {}", i, id.error()));
      if (!sample) return fail(std::format("view/markers/{}: {}", i, sample.error()));
      if (!mname) return fail(std::format("view/markers/{}: {}", i, mname.error()));
      bitoscope::capture::SavedMarker m;
      m.id = *id;
      m.sample = *sample;
      m.name = *mname;
      try {
        view.markers.push_back(std::move(m));
      } catch (const std::bad_alloc&) {
        return fail("out of memory reading view/markers");
      }
    }
  }

  if (H5Lexists(group.id(), "channel_colors", H5P_DEFAULT)) {
    Handle colors(H5Gopen2(group.id(), "channel_colors", H5P_DEFAULT), H5Gclose);
    if (!colors.ok()) return fail("cannot open view/channel_colors");
    for (uint32_t i = 0;; ++i) {
      const std::string name = std::to_string(i);
      if (!H5Lexists(colors.id(), name.c_str(), H5P_DEFAULT)) break;
      Handle entry(H5Gopen2(colors.id(), name.c_str(), H5P_DEFAULT), H5Gclose);
      if (!entry.ok()) return fail(std::format("cannot open view/channel_colors/{}", i));
      auto color = requireStringAttr(entry.id(), "color");
      if (!color) return fail(std::format("view/channel_colors/{}: {}", i, color.error()));
      try {
        view.channel_colors.push_back(*color);
      } catch (const std::bad_alloc&) {
        return fail("out of memory reading view/channel_colors");
      }
    }
    if (view.channel_colors.size() != channel_count) {
      return fail(std::format("view/channel_colors names {} channel color{}, capture has {} channel{}",
                              view.channel_colors.size(), view.channel_colors.size() == 1 ? "" : "s",
                              channel_count, channel_count == 1 ? "" : "s"));
    }
  }

  if (H5Lexists(group.id(), "channel_names", H5P_DEFAULT)) {
    auto names = readChannelNames(group.id(), channel_count);
    if (names) {
      view.channel_names = std::move(*names);
    } else {
      warnings.push_back(std::format("saved channel names are malformed and were dropped: {}", names.error()));
    }
  }

  return view;
}

std::optional<bitoscope::capture::ViewState> readView(hid_t device_group, uint32_t channel_count,
                                                       std::vector<std::string>& warnings) {
  if (!H5Lexists(device_group, "view", H5P_DEFAULT)) return std::nullopt;
  std::vector<std::string> fieldWarnings;
  auto view = readViewInner(device_group, channel_count, fieldWarnings);
  if (!view) {
    warnings.push_back(std::format("saved view state is malformed and was dropped: {}", view.error()));
    return std::nullopt;
  }
  for (std::string& w : fieldWarnings) warnings.push_back(std::move(w));
  return std::move(*view);
}

struct RequestFields {
  uint64_t requested_samples = 0;
  std::optional<uint64_t> requested_pre_trigger_samples;
  std::optional<uint64_t> requested_post_trigger_samples;
  std::optional<uint64_t> stored_post_trigger_samples;
};

std::expected<RequestFields, std::string> readRequestFields(hid_t group, uint64_t stored_samples,
                                                             bool triggered,
                                                             std::optional<uint64_t> origin,
                                                             std::optional<uint64_t> trigger) {
  RequestFields out;
  auto requested = requireScalarAttr<uint64_t>(group, "requested_sample_count", H5T_NATIVE_UINT64);
  if (!requested) return std::unexpected(requested.error());
  out.requested_samples = *requested;

  if (!triggered) return out;

  if (!origin || !trigger || *trigger < *origin) {
    return fail("triggered device's origin_sample_index/trigger_sample_index are inconsistent");
  }
  const uint64_t pre_held = *trigger - *origin;
  if (stored_samples < pre_held) {
    return fail(std::format(
        "device holds {} samples, less than {} before its own recorded trigger", stored_samples,
        pre_held));
  }

  auto req_pre = requireScalarAttr<uint64_t>(group, "requested_pre_trigger_sample_count", H5T_NATIVE_UINT64);
  if (!req_pre) return std::unexpected(req_pre.error());
  auto req_post = requireScalarAttr<uint64_t>(group, "requested_post_trigger_sample_count", H5T_NATIVE_UINT64);
  if (!req_post) return std::unexpected(req_post.error());
  auto held_post = requireScalarAttr<uint64_t>(group, "post_trigger_sample_count", H5T_NATIVE_UINT64);
  if (!held_post) return std::unexpected(held_post.error());

  out.requested_pre_trigger_samples = *req_pre;
  out.stored_post_trigger_samples = *held_post;
  out.requested_post_trigger_samples = *req_post;
  return out;
}

bool isDeflatedInWindows(hid_t dataset, uint64_t window_bytes) {
  Handle plist(H5Dget_create_plist(dataset), H5Pclose);
  if (!plist.ok() || H5Pget_layout(plist.id()) != H5D_CHUNKED) return false;
  hsize_t chunk = 0;
  if (H5Pget_chunk(plist.id(), 1, &chunk) != 1 || chunk != window_bytes) return false;
  if (H5Pget_nfilters(plist.id()) != 1) return false;
  unsigned flags = 0;
  size_t parameter_count = 0;
  return H5Pget_filter2(plist.id(), 0, &flags, &parameter_count, nullptr, 0, nullptr, nullptr) == H5Z_FILTER_DEFLATE;
}

bool readChannelBytes(const ChannelEntry& channel, uint64_t byte_offset, uint64_t n_bytes, std::span<uint8_t> row) {
  const hsize_t start = static_cast<hsize_t>(byte_offset);
  const hsize_t count = static_cast<hsize_t>(n_bytes);
  Handle file_space(H5Dget_space(channel.dataset.id()), H5Sclose);
  if (!file_space.ok() ||
      H5Sselect_hyperslab(file_space.id(), H5S_SELECT_SET, &start, nullptr, &count, nullptr) < 0) {
    return false;
  }
  Handle mem_space(H5Screate_simple(1, &count, nullptr), H5Sclose);
  return mem_space.ok() && H5Dread(channel.dataset.id(), H5T_NATIVE_UINT8, mem_space.id(), file_space.id(),
                                   H5P_DEFAULT, row.data()) >= 0;
}

struct WindowJob {
  std::vector<std::unique_ptr<uint8_t[]>> stored;
  std::vector<size_t> stored_bytes;
  std::optional<std::string> failure;
};

struct LoadShape {
  uint32_t workers = 1;
  uint32_t depth = 1;
};

LoadShape loadShape(const Hdf5ReaderOptions& options, const CapturePlan& plan) {
  constexpr uint32_t kMostWorkers = 8;
  constexpr uint64_t kInFlightBytes = 64ULL << 20;
  constexpr uint64_t kBuffersPerWindow = 3;
  const uint32_t machine =
      (std::min)(bitoscope::acquisition::defaultClearingWorkers(std::thread::hardware_concurrency()), kMostWorkers);
  const uint64_t workers = (std::max)(options.pack_workers.value_or(machine), uint32_t{1});
  const uint64_t window_bytes = uint64_t{plan.channels()} * (plan.samplesPerWindow() / 8);
  const uint64_t by_bytes = (std::max)(kInFlightBytes / (kBuffersPerWindow * window_bytes), uint64_t{2});
  const uint64_t depth = (std::min)({workers + 1, by_bytes, plan.slots()});
  return {static_cast<uint32_t>((std::min)(workers, depth)), static_cast<uint32_t>(depth)};
}

std::expected<RawCaptureBuffer, std::string> buildBuffer(std::vector<ChannelEntry>& channels,
                                                          uint64_t sample_rate_milli_hz,
                                                          uint64_t stored_samples,
                                                          const Hdf5ReaderOptions& options,
                                                          MemoryProbe& probe, BlockSource& blocks) {
  auto plan = makeCapturePlan(static_cast<uint32_t>(channels.size()), sample_rate_milli_hz,
                              stored_samples);
  if (!plan) return fail(plan.error());

  constexpr uint32_t kNoPackWorkers = 0;
  auto held_back =
      bitoscope::capture::captureHeldBackBytes(*plan, kNoPackWorkers, options.build_pyramid);
  if (!held_back) return fail(held_back.error());

  AllocatorSettings settings;
  settings.budget.explicit_budget = plan->totalBytes();
  settings.budget.fit_explicit_to_ceiling = true;
  if (options.reserve_bytes) settings.budget.reserve = *options.reserve_bytes;
  settings.budget.held_back = *held_back;
  if (options.build_pyramid) settings.budget.held_back_for_arena = bitoscope::capture::pyramidPlanesHeldBack(*plan);
  settings.arena_block_bytes = options.arena_block_bytes;
  settings.clearing_workers = options.clearing_workers.value_or(
      bitoscope::acquisition::defaultClearingWorkers(std::thread::hardware_concurrency()));
  settings.clearing_threads = options.clearing_threads;

  auto budget = bitoscope::capture::computeBudget(probe, settings.budget);
  if (!budget) {
    return fail(std::format("cannot reserve memory to load this capture ({} bytes): {}",
                            plan->totalBytes(), budget.error()));
  }
  if (budget->bytes < plan->slotBytes(0)) {
    return fail(std::format("not enough memory to load capture: its first {} samples on {} channels need {} bytes "
                            "unpacked and only {} bytes can be spared; free memory and open file again",
                            plan->slotSamples(0), plan->channels(), plan->slotBytes(0), budget->bytes));
  }
  auto arena = bitoscope::capture::planGrowingArena(*plan, *budget, probe, blocks, settings);
  if (!arena) {
    return fail(std::format("cannot reserve memory to load this capture ({} bytes): {}",
                            plan->totalBytes(), arena.error()));
  }
  const uint64_t first_block_bytes = arena->reservation.blockSize(0);
  auto created = RawCaptureBuffer::create(std::move(arena->reservation), options.build_pyramid);
  if (!created) return fail(created.error());
  RawCaptureBuffer buffer = std::move(*created);

  const uint64_t bytes_per_full_window = plan->samplesPerWindow() / 8;
  std::vector<bool> deflated;
  deflated.reserve(channels.size());
  for (const ChannelEntry& c : channels) deflated.push_back(isDeflatedInWindows(c.dataset.id(), bytes_per_full_window));

  const LoadShape shape = loadShape(options, *plan);
  std::vector<WindowJob> jobs(shape.depth + 1);
  for (WindowJob& job : jobs) {
    job.stored.resize(channels.size());
    job.stored_bytes.assign(channels.size(), 0);
    for (size_t c = 0; c < channels.size(); ++c) {
      if (deflated[c]) job.stored[c] = std::make_unique_for_overwrite<uint8_t[]>(compressBound(static_cast<uLong>(bytes_per_full_window)));
    }
  }
  WindowPool leases(shape.depth + 1, plan->channels(), plan->samplesPerWindow());

  std::atomic<bool> failed{false};
  std::string failure;
  uint64_t committed = 0;
  const auto stop = [&](std::string text) {
    failure = std::move(text);
    failed.store(true, std::memory_order_release);
  };
  const auto commit = [&](const WindowInfo& info, const std::expected<bitoscope::capture::PackedWindow, std::string>& packed) {
    if (failed.load(std::memory_order_acquire)) return;
    const uint64_t seq = committed++;
    try {
      if (!packed) {
        stop(jobs[seq % jobs.size()].failure
                 ? packed.error()
                 : hdf5Text(std::format("cannot store window at sample {}: {}", info.first_sample_index, packed.error())));
        return;
      }
      auto outcome = buffer.appendPacked(*packed, &arena->growth);
      if (!outcome) {
        stop(hdf5Text(std::format("cannot store window at sample {}: {}", info.first_sample_index, outcome.error())));
      } else if (*outcome != bitoscope::capture::AppendOutcome::Stored && arena->growth.bytesTaken() == 0) {
        stop(hdf5Text(std::format("not enough memory to load capture: first memory block of {} bytes could not be taken "
                                  "(file's samples need {} bytes unpacked); free memory and open file again",
                                  first_block_bytes, plan->totalBytes())));
      } else if (*outcome != bitoscope::capture::AppendOutcome::Stored) {
        stop(hdf5Text(std::format(
            "not enough memory to load capture: memory ran out near sample {} of {}, its packed samples need more than "
            "{} bytes ({} bytes unpacked); free memory and open file again",
            info.first_sample_index, stored_samples, arena->growth.bytesTaken(), plan->totalBytes())));
      }
    } catch (const std::bad_alloc&) {
      stop(hdf5Text("out of memory loading capture"));
    }
  };
  const auto prepare = [&](uint64_t seq, bitoscope::host::WindowLease& lease) -> std::expected<void, std::string> {
    WindowJob& job = jobs[seq % jobs.size()];
    if (job.failure) return std::unexpected(*job.failure);
    for (uint32_t c = 0; c < channels.size(); ++c) {
      const std::span<uint8_t> row = lease.channel(c);
      if (job.stored_bytes[c] != 0) {
        const std::span<const uint8_t> stored(job.stored[c].get(), job.stored_bytes[c]);
        if (!inflateChunk(stored, true, row)) {
          job.failure = hdf5Text(std::format("cannot read channel {} at byte {}", channels[c].index, seq * bytes_per_full_window));
          return std::unexpected(*job.failure);
        }
      }
    }
    return {};
  };

  bitoscope::capture::PackPoolConfig pool_config;
  pool_config.workers = shape.workers;
  pool_config.depth = shape.depth;
  pool_config.prepare = prepare;
  pool_config.hooks = options.pack_hooks_for_test;
  auto pool = bitoscope::capture::PackPool::create(buffer, std::move(pool_config), commit);
  if (!pool) return fail(pool.error());

  std::optional<std::string> stopped;
  for (uint64_t w = 0; w < plan->slots() && !failed.load(std::memory_order_acquire); ++w) {
    auto lease = leases.acquire();
    if (!lease) {
      stopped = "could not acquire window buffer while loading";
      break;
    }
    const uint64_t n = plan->slotSamples(w);
    const uint64_t byte_offset = w * bytes_per_full_window;
    const uint64_t n_bytes = (n + 7) / 8;

    WindowInfo& info = lease->info();
    info.first_sample_index = w * plan->samplesPerWindow();
    info.sample_rate_milli_hz = sample_rate_milli_hz;
    info.num_channels = plan->channels();
    info.num_samples = static_cast<uint32_t>(n);

    WindowJob& job = jobs[w % jobs.size()];
    job.failure.reset();
    for (uint32_t c = 0; c < channels.size() && !job.failure; ++c) {
      job.stored_bytes[c] = 0;
      if (deflated[c]) {
        size_t stored_bytes = compressBound(static_cast<uLong>(bytes_per_full_window));
        uint32_t filter_mask = 0;
        const hsize_t chunk_offset = static_cast<hsize_t>(byte_offset);
        if (H5Dread_chunk2(channels[c].dataset.id(), H5P_DEFAULT, &chunk_offset, &filter_mask, job.stored[c].get(),
                           &stored_bytes) >= 0 &&
            filter_mask == 0 && stored_bytes != 0) {
          job.stored_bytes[c] = stored_bytes;
          continue;
        }
      }
      if (!readChannelBytes(channels[c], byte_offset, n_bytes, lease->channel(c))) {
        job.failure = hdf5Text(std::format("cannot read channel {} at byte {}", channels[c].index, byte_offset));
      }
    }
    if (auto dispatched = (*pool)->dispatch(std::move(*lease), bitoscope::host::WaitLimit{}); !dispatched) {
      stopped = "loading stopped before every window was handed over";
      break;
    }
    if (job.failure) break;
  }
  (*pool)->drain();
  pool->reset();
  if (failed.load(std::memory_order_acquire)) return std::unexpected(failure);
  if (stopped) return fail(*stopped);
  if (auto done = buffer.finalize(); !done) return fail(done.error());
  if (auto released = buffer.releaseUnusedBlocks(); !released) return fail(released.error());
  return buffer;
}

std::expected<LoadedDevice, std::string> readDeviceGroup(hid_t group, const Hdf5ReaderOptions& options,
                                                         MemoryProbe& probe, BlockSource& blocks) {
  LoadedDeviceMetadata meta;

  auto device_name = requireStringAttr(group, "device");
  auto offset = requireScalarAttr<int64_t>(group, "alignment_offset_ps", H5T_NATIVE_INT64);
  if (!device_name) return std::unexpected(device_name.error());
  if (!offset) return std::unexpected(offset.error());
  meta.device = *device_name;
  meta.alignment_offset_ps = *offset;

  auto rate = requireScalarAttr<uint64_t>(group, "sample_rate_milli_hz", H5T_NATIVE_UINT64);
  if (!rate || *rate == 0) return fail("missing or zero sample_rate_milli_hz");
  auto stored = requireScalarAttr<uint64_t>(group, "sample_count", H5T_NATIVE_UINT64);
  if (!stored || *stored == 0) return fail("missing or zero sample_count");
  auto channel_count = requireScalarAttr<uint32_t>(group, "channel_count", H5T_NATIVE_UINT32);
  if (!channel_count || *channel_count == 0) return fail("missing or zero channel_count");
  if (*channel_count > kMaxChannels) {
    return fail(std::format("channel_count {} exceeds {} channels this reader supports",
                            *channel_count, kMaxChannels));
  }
  meta.sample_rate_milli_hz = *rate;
  meta.stored_samples = *stored;
  meta.capture_id = readScalarAttr<uint64_t>(group, "capture_id", H5T_NATIVE_UINT64).value_or(0);

  const bool has_origin = attrExists(group, "origin_sample_index");
  const bool has_trigger = attrExists(group, "trigger_sample_index");
  if (has_origin != has_trigger) {
    return fail("has only one of origin_sample_index and trigger_sample_index");
  }
  if (has_origin) {
    meta.origin_sample_index = readScalarAttr<uint64_t>(group, "origin_sample_index", H5T_NATIVE_UINT64);
    meta.trigger_sample_index = readScalarAttr<uint64_t>(group, "trigger_sample_index", H5T_NATIVE_UINT64);
  }

  auto request = readRequestFields(group, *stored, has_origin, meta.origin_sample_index,
                                   meta.trigger_sample_index);
  if (!request) return std::unexpected(request.error());
  meta.requested_samples = request->requested_samples;
  meta.requested_pre_trigger_samples = request->requested_pre_trigger_samples;
  meta.requested_post_trigger_samples = request->requested_post_trigger_samples;
  meta.stored_post_trigger_samples = request->stored_post_trigger_samples;

  meta.short_reason = readOptionalStringAttr(group, "short_reason");
  meta.memory_full_device = readScalarAttr<uint32_t>(group, "memory_full_device", H5T_NATIVE_UINT32);

  auto decoder_setup = readDecoderSetup(group, meta.decoder_presentation, meta.decoder_versions);
  if (!decoder_setup) return std::unexpected(decoder_setup.error());
  meta.decoder_setup = std::move(*decoder_setup);

  auto device_properties = readDeviceProperties(group);
  if (!device_properties) return std::unexpected(device_properties.error());
  meta.device_properties = std::move(*device_properties);

  if (!H5Lexists(group, "channels", H5P_DEFAULT)) return fail("missing channels group");
  Handle channels_group(H5Gopen2(group, "channels", H5P_DEFAULT), H5Gclose);
  if (!channels_group.ok()) return fail("cannot open channels group");
  const uint64_t expected_bytes = (*stored + 7) / 8;
  auto channels = discoverChannels(channels_group.id(), *channel_count, expected_bytes);
  if (!channels) return std::unexpected(channels.error());

  for (const ChannelEntry& c : *channels) {
    try {
      meta.channels.push_back(SavedChannel{c.index, c.name});
    } catch (const std::bad_alloc&) {
      return fail("out of memory listing channels");
    }
  }

  meta.view = readView(group, *channel_count, meta.warnings);

  auto buffer = buildBuffer(*channels, *rate, *stored, options, probe, blocks);
  if (!buffer) return std::unexpected(buffer.error());

  LoadedDevice device{std::move(*buffer), std::move(meta)};
  return device;
}

}

std::expected<LoadedSession, std::string> loadSession(const std::filesystem::path& path,
                                                       const Hdf5ReaderOptions& options) {
  const std::lock_guard<std::recursive_mutex> lock(libraryMutex());
  try {
    H5Eset_auto2(H5E_DEFAULT, nullptr, nullptr);

    bitoscope::capture::SystemMemoryProbe default_probe;
    bitoscope::capture::SystemBlockSource default_blocks;
    MemoryProbe& probe = options.probe != nullptr ? *options.probe : default_probe;
    BlockSource& blocks = options.blocks != nullptr ? *options.blocks : default_blocks;

    const std::string native = utf8Path(path);
    Handle file(H5Fopen(native.c_str(), H5F_ACC_RDONLY, H5P_DEFAULT), H5Fclose);
    if (!file.ok()) {
      return fail(std::format("cannot open '{}': not readable HDF5 file", native));
    }

    auto version = requireScalarAttr<uint32_t>(file.id(), "format_version", H5T_NATIVE_UINT32);
    if (!version) {
      return fail(std::format("'{}' is not bitoscope capture file: no format_version attribute",
                              native));
    }
    if (*version != kFormatVersion) {
      return fail(std::format("'{}' is format version {}, which this reader does not support (only {})", native,
                              *version, kFormatVersion));
    }
    auto bit_order = readOptionalStringAttr(file.id(), "bit_order");
    if (!bit_order || *bit_order != "lsb-first") {
      return fail(std::format("'{}' is format version {} but its bit_order attribute is missing or not 'lsb-first'",
                              native, *version));
    }

    LoadedSession session;
    session.format_version = *version;

    auto device_count = requireScalarAttr<uint32_t>(file.id(), "device_count", H5T_NATIVE_UINT32);
    if (!device_count || *device_count == 0) return fail("missing or zero device_count");
    auto locked = readScalarAttr<uint8_t>(file.id(), "alignment_locked", H5T_NATIVE_UINT8);
    session.alignment_locked = locked.value_or(0) != 0;
    session.short_reason = readOptionalStringAttr(file.id(), "short_reason");

    if (!H5Lexists(file.id(), "devices", H5P_DEFAULT)) return fail("missing /devices group");
    Handle devices_group(H5Gopen2(file.id(), "devices", H5P_DEFAULT), H5Gclose);
    if (!devices_group.ok()) return fail("cannot open /devices group");

    try {
      session.devices.reserve(*device_count);
    } catch (const std::bad_alloc&) {
      return fail("out of memory listing session's devices");
    }
    for (uint32_t n = 0; n < *device_count; ++n) {
      const std::string name = std::to_string(n);
      if (!H5Lexists(devices_group.id(), name.c_str(), H5P_DEFAULT)) {
        return fail(std::format("missing /devices/{}", n));
      }
      Handle group(H5Gopen2(devices_group.id(), name.c_str(), H5P_DEFAULT), H5Gclose);
      if (!group.ok()) return fail(std::format("cannot open /devices/{}", n));
      auto device = readDeviceGroup(group.id(), options, probe, blocks);
      if (!device) return std::unexpected(std::format("device {}: {}", n, device.error()));
      session.devices.push_back(std::move(*device));
    }
    return session;
  } catch (const std::bad_alloc&) {
    return fail(std::format("out of memory reading '{}'", utf8Path(path)));
  }
}

}
