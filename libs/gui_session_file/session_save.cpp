/*
 * SPDX-License-Identifier: MIT
 */

#include "session_save.h"

#include <algorithm>
#include <ctime>
#include <exception>
#include <format>
#include <map>
#include <new>
#include <system_error>
#include <type_traits>
#include <utility>
#include <variant>

#include <device_properties.h>
#include <hdf5_capture_writer.h>
#include <ipc_la_acquisition.h>

namespace bitoscope::gui_session_file {

namespace {

namespace props = ipc::props;

std::optional<capture::SavedPropertyValue> exactValue(const props::Property& property,
                                                     const props::PropertyValue& value) {
  return std::visit(
      [&](const auto& spec) -> std::optional<capture::SavedPropertyValue> {
        using T = std::decay_t<decltype(spec)>;
        if constexpr (std::is_same_v<T, props::IntegerSpec>) {
          const auto* v = std::get_if<int64_t>(&value.value);
          if (v == nullptr) return std::nullopt;
          return capture::SavedIntegerValue{*v, 0, std::string(device_properties::unitSymbol(property.unit))};
        } else if constexpr (std::is_same_v<T, props::DecimalSpec>) {
          const auto* v = std::get_if<int64_t>(&value.value);
          if (v == nullptr) return std::nullopt;
          return capture::SavedIntegerValue{*v, spec.digits, std::string(device_properties::unitSymbol(property.unit))};
        } else if constexpr (std::is_same_v<T, props::BooleanSpec>) {
          const auto* v = std::get_if<bool>(&value.value);
          if (v == nullptr) return std::nullopt;
          return capture::SavedPropertyValue{*v};
        } else if constexpr (std::is_same_v<T, props::EnumerationSpec>) {
          const auto* idx = std::get_if<props::EnumIndex>(&value.value);
          if (idx == nullptr || idx->index >= spec.items.size()) return std::nullopt;
          const auto& item = spec.items[idx->index];
          capture::SavedEnumValue out;
          if (const auto* s = std::get_if<std::string>(&item)) {
            out.name = *s;
          } else if (const auto* n = std::get_if<int64_t>(&item)) {
            out.number = *n;
            out.digits = spec.digits;
            out.unit = std::string(device_properties::unitSymbol(property.unit));
          } else {
            return std::nullopt;
          }
          return capture::SavedPropertyValue{std::move(out)};
        } else {
          return std::nullopt;
        }
      },
      property.type);
}

std::string utf8(const std::filesystem::path& path) {
  const std::u8string text = path.u8string();
  return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}

capture::ViewState viewStateOf(const SaveView& view) {
  capture::ViewState state;
  for (const waveform_view::Marker& marker : view.markers.markers()) {
    state.markers.push_back({marker.id, marker.sample, marker.name});
  }
  if (view.cursors) {
    state.cursors = capture::SavedCursorPair{view.cursors->first(), view.cursors->second(), view.showCursors};
  }
  for (const waveform_view::ColorChoice& color : view.channelColors) {
    state.channel_colors.push_back(waveform_view::toText(color));
  }
  if (view.laneOrder) state.lane_order = waveform_view::toTextWithDecoderKeys(*view.laneOrder, view.decoderIds);
  if (std::ranges::any_of(view.channelUserNames, [](const std::string& name) { return !name.empty(); })) {
    state.channel_names = view.channelUserNames;
  }
  return state;
}

}

std::expected<capture::CaptureMetadata, std::string> captureMetadataFor(const gui_capture::CaptureOutcome& kept,
                                                                        const SaveView& view) {
  if (!kept.kept()) {
    return std::unexpected(std::string("there is no capture to save: last run kept nothing"));
  }
  if (!kept.buffer) {
    return std::unexpected(std::string("capture's samples are not held any more; nothing can be saved"));
  }
  const std::uint32_t channels = kept.buffer->plan().channels();
  if (view.deviceChannels.size() != channels || view.channelNames.size() != channels) {
    return std::unexpected(std::format("capture has {} channels, but view shows {} lanes", channels,
                                       view.deviceChannels.size()));
  }
  if (view.channelColors.size() != channels) {
    return std::unexpected(std::format("capture has {} channels, but view has {} channel colors", channels,
                                       view.channelColors.size()));
  }
  if (!view.channelUserNames.empty() && view.channelUserNames.size() != channels) {
    return std::unexpected(std::format("capture has {} channels, but view holds {} channel names", channels,
                                       view.channelUserNames.size()));
  }

  capture::CaptureMetadata m;
  for (std::size_t k = 0; k < channels; ++k) {
    const std::uint32_t device = view.deviceChannels[k];
    m.channels.push_back(
        {device, view.channelNames[k].empty() ? std::format("D{}", device) : view.channelNames[k]});
  }
  m.capture_id = kept.captureId;
  m.origin_sample_index = kept.originSampleIndex;
  m.trigger_sample_index = kept.triggerSampleIndex;
  m.requested_samples = kept.requestedSamples;
  m.requested_pre_trigger_samples = kept.requestedPreTriggerSamples;
  m.requested_post_trigger_samples = kept.requestedPostTriggerSamples;
  m.memory_shortfall = kept.shortfall;
  m.extent = kept.extent;
  m.device = kept.device;
  m.device_properties = view.deviceProperties;
  m.decoder_setup = view.decoderSetup;
  m.decoder_presentation = view.decoderPresentation;
  m.decoder_versions = view.decoderVersions;

  m.view = viewStateOf(view);
  return m;
}

DecoderSetupForSave decoderSetupForSave(std::span<const std::pair<std::string, std::string>> loadedSetup,
                                        std::span<const std::size_t> skipped,
                                        std::span<const std::pair<std::string, std::string>> currentSetup,
                                        std::span<const std::string> loadedPresentation,
                                        std::span<const std::string> currentPresentation,
                                        std::span<const std::optional<std::size_t>> unchanged,
                                        std::span<const capture::DecoderVersions> loadedVersions,
                                        std::span<const capture::DecoderVersions> currentVersions) {
  std::vector<bool> isSkipped(loadedSetup.size(), false);
  for (const std::size_t j : skipped) {
    if (j < isSkipped.size()) isSkipped[j] = true;
  }
  DecoderSetupForSave out;
  out.setup.assign(currentSetup.begin(), currentSetup.end());
  for (std::size_t i = 0; i < out.setup.size() && i < unchanged.size(); ++i) {
    const std::optional<std::size_t> entry = unchanged[i];
    if (entry && *entry < loadedSetup.size() && !isSkipped[*entry] && loadedSetup[*entry].first == out.setup[i].first) {
      out.setup[i] = loadedSetup[*entry];
    }
  }
  for (std::size_t i = 0; i < currentSetup.size(); ++i) {
    out.presentation.push_back(i < currentPresentation.size() ? currentPresentation[i] : std::string());
    out.versions.push_back(i < currentVersions.size() ? currentVersions[i] : capture::DecoderVersions{});
  }
  std::vector<std::size_t> order;
  for (std::size_t j = 0; j < loadedSetup.size(); ++j) {
    if (isSkipped[j]) order.push_back(j);
  }
  for (const std::size_t j : order) {
    const std::size_t at = (std::min)(j, out.setup.size());
    out.setup.insert(out.setup.begin() + static_cast<std::ptrdiff_t>(at), loadedSetup[j]);
    out.presentation.insert(out.presentation.begin() + static_cast<std::ptrdiff_t>(at),
                            j < loadedPresentation.size() ? loadedPresentation[j] : std::string());
    out.versions.insert(out.versions.begin() + static_cast<std::ptrdiff_t>(at),
                        j < loadedVersions.size() ? loadedVersions[j] : capture::DecoderVersions{});
  }
  if (std::ranges::all_of(out.presentation, [](const std::string& p) { return p.empty(); })) out.presentation.clear();
  if (std::ranges::all_of(out.versions, [](const capture::DecoderVersions& v) { return v == capture::DecoderVersions{}; })) {
    out.versions.clear();
  }
  return out;
}

DecoderUpdate decoderSetupForUpdate(std::span<const std::pair<std::string, std::string>> loadedSetup,
                                    std::span<const std::size_t> skipped, std::span<const DecoderToSave> current,
                                    std::span<const std::string> loadedPresentation,
                                    std::span<const capture::DecoderVersions> loadedVersions) {
  std::vector<bool> isSkipped(loadedSetup.size(), false);
  for (const std::size_t j : skipped) {
    if (j < isSkipped.size()) isSkipped[j] = true;
  }
  std::vector<bool> claimed(loadedSetup.size(), false);
  DecoderUpdate out;
  for (std::size_t i = 0; i < current.size(); ++i) {
    const DecoderToSave& decoder = current[i];
    const std::optional<std::size_t> entry = decoder.unchangedEntry;
    if (entry && *entry < loadedSetup.size() && !isSkipped[*entry] && !claimed[*entry] &&
        loadedSetup[*entry].first == decoder.name) {
      claimed[*entry] = true;
      out.setup.push_back(loadedSetup[*entry]);
    } else {
      out.setup.emplace_back(decoder.name, decoder.specText);
    }
    out.presentation.push_back(decoder.presentation);
    out.versions.push_back(decoder.versions);
    out.currentAt.push_back(i);
  }
  for (std::size_t j = 0; j < loadedSetup.size(); ++j) {
    if (!isSkipped[j]) continue;
    const std::size_t at = (std::min)(j, out.setup.size());
    out.setup.insert(out.setup.begin() + static_cast<std::ptrdiff_t>(at), loadedSetup[j]);
    out.presentation.insert(out.presentation.begin() + static_cast<std::ptrdiff_t>(at),
                            j < loadedPresentation.size() ? loadedPresentation[j] : std::string());
    out.versions.insert(out.versions.begin() + static_cast<std::ptrdiff_t>(at),
                        j < loadedVersions.size() ? loadedVersions[j] : capture::DecoderVersions{});
    for (std::size_t& k : out.currentAt) {
      if (k >= at) ++k;
    }
    out.skippedAt.push_back(at);
  }
  if (std::ranges::all_of(out.presentation, [](const std::string& p) { return p.empty(); })) out.presentation.clear();
  if (std::ranges::all_of(out.versions, [](const capture::DecoderVersions& v) { return v == capture::DecoderVersions{}; })) {
    out.versions.clear();
  }
  return out;
}

std::expected<void, std::string> updateSessionDecoders(const std::filesystem::path& path, std::uint32_t deviceIndex,
                                                        const DecoderUpdate& update) {
  try {
    return hdf5::updateSessionDecoders(path, deviceIndex, update.setup, update.presentation, update.versions);
  } catch (const std::bad_alloc&) {
    return std::unexpected(std::string("out of memory while saving decoders"));
  } catch (const std::exception& e) {
    return std::unexpected(std::format("decoders could not be saved: {}", e.what()));
  }
}

std::vector<capture::SavedProperty> savedDeviceProperties(std::span<const props::PropertyValue> sent,
                                                          const ipc::DeviceInfo& device) {
  std::map<std::string, props::PropertyValue> analyzerSet;
  for (const props::PropertyValue& v : sent) analyzerSet.emplace(v.id, v);

  std::vector<capture::SavedProperty> out;
  for (const ipc::FacetInfo& facet : device.facets) {
    if (facet.name != ipc::la::kFacetName) continue;
    for (const auto& listing : device_properties::listProperties(facet.properties)) {
      const props::Property& property = *listing.property;
      if (property.changeableDuringCapture) continue;
      props::PropertyValue value;
      if (const auto it = analyzerSet.find(property.id); it != analyzerSet.end()) {
        value = it->second;
      } else {
        auto def = device_properties::effectiveDefault(property);
        if (!def.has_value()) continue;
        value = std::move(*def);
      }
      auto formatted = device_properties::formatValue(property, value);
      if (!formatted.has_value()) continue;
      auto exact = exactValue(property, value);
      if (!exact.has_value()) continue;
      capture::SavedProperty saved;
      saved.facet = facet.name;
      saved.id = property.id;
      saved.name = property.name;
      saved.formatted = std::move(*formatted);
      saved.value = std::move(*exact);
      out.push_back(std::move(saved));
    }
  }
  return out;
}

std::expected<void, std::string> writeSessionFile(const std::filesystem::path& path,
                                                  const gui_capture::CaptureOutcome& kept,
                                                  const capture::CaptureMetadata& metadata, std::uint32_t gzipLevel) {
  if (!kept.kept() || !kept.buffer) {
    return std::unexpected(std::string("there is no capture to save"));
  }
  hdf5::Hdf5WriterOptions options;
  options.gzip_level = gzipLevel;
  try {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
      hdf5::Hdf5CaptureWriter writer(path, options);
      return writer.write(*kept.buffer, metadata);
    }
    // WHY: Replace an existing file only by a complete new one written beside it: the writer never overwrites.
    std::filesystem::path fresh = path;
    fresh += ".saving.h5";
    if (std::filesystem::exists(fresh, ec)) {
      return std::unexpected(std::format("{} already exists (leftover of earlier save?); remove it first",
                                         utf8(fresh)));
    }
    hdf5::Hdf5CaptureWriter writer(fresh, options);
    if (auto written = writer.write(*kept.buffer, metadata); !written) {
      return written;
    }
    std::filesystem::rename(fresh, path, ec);
    if (ec) {
      const std::string message = ec.message();
      std::filesystem::remove(fresh, ec);
      return std::unexpected(std::format("cannot replace {}: {}", utf8(path), message));
    }
    return {};
  } catch (const std::bad_alloc&) {
    return std::unexpected(std::string("out of memory while saving capture"));
  } catch (const std::exception& e) {
    return std::unexpected(std::format("capture could not be saved: {}", e.what()));
  }
}

std::expected<void, std::string> updateSessionView(const std::filesystem::path& path, std::uint32_t deviceIndex,
                                                    const SaveView& view) {
  try {
    return hdf5::updateSessionView(path, deviceIndex, viewStateOf(view));
  } catch (const std::bad_alloc&) {
    return std::unexpected(std::string("out of memory while saving view"));
  } catch (const std::exception& e) {
    return std::unexpected(std::format("view could not be saved: {}", e.what()));
  }
}

CivilTime localCivilTime(std::chrono::system_clock::time_point when) {
  const std::time_t t = std::chrono::system_clock::to_time_t(when);
  std::tm local{};
#ifdef _WIN32
  if (localtime_s(&local, &t) != 0) return {};
#else
  if (localtime_r(&t, &local) == nullptr) return {};
#endif
  return CivilTime{local.tm_year + 1900, local.tm_mon + 1, local.tm_mday, local.tm_hour, local.tm_min};
}

std::string suggestedFileName(std::string_view sessionName, const CivilTime& when) {
  std::string name;
  for (const char c : sessionName) {
    const auto u = static_cast<unsigned char>(c);
    const bool bad = u < 0x20 || c == '<' || c == '>' || c == ':' || c == '"' || c == '/' || c == '\\' || c == '|' ||
                     c == '?' || c == '*';
    name += bad ? '_' : c;
  }
  while (!name.empty() && (name.back() == '.' || name.back() == ' ')) {
    name.pop_back();
  }
  if (name.empty()) {
    name = "capture";
  }
  return std::format("{} {:04}-{:02}-{:02} {:02}-{:02}.h5", name, when.year, when.month, when.day, when.hour,
                     when.minute);
}

}
