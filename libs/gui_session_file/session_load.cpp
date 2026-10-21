/*
 * SPDX-License-Identifier: MIT
 */

#include "session_load.h"

#include <algorithm>
#include <exception>
#include <format>
#include <memory>
#include <new>
#include <utility>

#include "heap_trim.h"

namespace bitoscope::gui_session_file {

namespace {

std::optional<capture::ShortReason> shortReasonOf(std::string_view text) {
  if (text == "memory") return capture::ShortReason::Memory;
  if (text == "stopped by user") return capture::ShortReason::StoppedByUser;
  // WHY: Accept both spellings of this reason: older files hold the one with an article.
  if (text == "stopped with session" || text == "stopped with the session") {
    return capture::ShortReason::StoppedWithSession;
  }
  return std::nullopt;
}

gui_capture::OutcomeKind kindOf(capture::ShortReason reason) {
  switch (reason) {
    case capture::ShortReason::Memory:
      return gui_capture::OutcomeKind::ShortMemory;
    case capture::ShortReason::StoppedByUser:
      return gui_capture::OutcomeKind::ShortStoppedByUser;
    case capture::ShortReason::StoppedWithSession:
      return gui_capture::OutcomeKind::ShortStoppedWithSession;
  }
  return gui_capture::OutcomeKind::ShortMemory;
}

std::expected<LoadedPanel, std::string> panelOf(hdf5::LoadedDevice device, std::size_t index) {
  hdf5::LoadedDeviceMetadata& meta = device.metadata;
  LoadedPanel p;
  gui_capture::CaptureOutcome& o = p.outcome;

  o.kind = gui_capture::OutcomeKind::Complete;
  if (meta.short_reason) {
    const std::optional<capture::ShortReason> reason = shortReasonOf(*meta.short_reason);
    if (!reason) {
      return std::unexpected(std::format(
          "device {} of file is short capture for reason this version does not know ('{}'); it is not opened",
          index, *meta.short_reason));
    }
    o.kind = kindOf(*reason);
    capture::MemoryShortfall s;
    s.requested_samples = meta.requested_samples;
    s.stored_samples = meta.stored_samples;
    s.requested_post_trigger_samples = meta.requested_post_trigger_samples;
    s.stored_post_trigger_samples =
        meta.requested_post_trigger_samples ? meta.stored_post_trigger_samples : std::nullopt;
    s.reason = *reason;
    s.memory_full_device = meta.memory_full_device;
    o.shortfall = s;
  }
  o.requestedSamples = meta.requested_samples;
  o.storedSamples = meta.stored_samples;
  o.requestedPostTriggerSamples = meta.requested_post_trigger_samples;
  o.storedPostTriggerSamples = meta.stored_post_trigger_samples;
  o.requestedPreTriggerSamples = meta.requested_pre_trigger_samples;
  o.originSampleIndex = meta.origin_sample_index;
  o.triggerSampleIndex = meta.trigger_sample_index;
  o.extent = {0, meta.stored_samples};
  o.device = meta.device;
  o.captureId = meta.capture_id;
  o.buffer = std::make_shared<capture::RawCaptureBuffer>(std::move(device.buffer));

  p.device = meta.device;
  p.sampleRateMilliHz = meta.sample_rate_milli_hz;
  p.deviceProperties = meta.device_properties;
  p.decoderSetup = meta.decoder_setup;
  p.decoderPresentation = std::move(meta.decoder_presentation);
  p.decoderVersions = std::move(meta.decoder_versions);
  p.warnings = meta.warnings;

  std::uint32_t highest = 0;
  for (const capture::SavedChannel& channel : meta.channels) {
    p.deviceChannels.push_back(channel.index);
    highest = (std::max)(highest, channel.index);
  }
  const std::size_t size = meta.channels.empty() ? 0 : static_cast<std::size_t>(highest) + 1;
  p.channelNames.assign(size, std::string());
  p.channelUserNames.assign(size, std::string());
  p.channelColors.resize(size);
  for (std::size_t c = 0; c < size; ++c) {
    p.channelColors[c] = waveform_view::ColorChoice::palette(static_cast<std::uint32_t>(c));
  }
  for (const capture::SavedChannel& channel : meta.channels) {
    p.channelNames[channel.index] = channel.name;
  }

  if (meta.view) {
    const capture::ViewState& view = *meta.view;
    if (!view.channel_colors.empty()) {
      if (view.channel_colors.size() != meta.channels.size()) {
        p.warnings.push_back(std::format("file names {} channel colors for {} channels; palette is used",
                                         view.channel_colors.size(), meta.channels.size()));
      } else {
        for (std::size_t k = 0; k < meta.channels.size(); ++k) {
          const std::uint32_t c = meta.channels[k].index;
          if (auto color = waveform_view::fromText(view.channel_colors[k])) {
            p.channelColors[c] = *color;
          } else {
            p.warnings.push_back(std::format("channel {}'s color '{}' is not one this version reads ({}); its "
                                             "palette color is used",
                                             meta.channels[k].name, view.channel_colors[k], color.error()));
          }
        }
      }
    }
    // WHY: Lane arrangement stays text here: decoder lanes are named by decoder keys that only the GUI can resolve
    // after it rebuilds the decoders.
    if (view.lane_order) p.laneOrderText = *view.lane_order;
    p.decoderVisibilityText = view.decoder_visibility;
    if (view.channel_names.size() == meta.channels.size()) {
      for (std::size_t k = 0; k < meta.channels.size(); ++k) {
        p.channelUserNames[meta.channels[k].index] = view.channel_names[k];
      }
    }
    std::vector<waveform_view::Marker> markers;
    for (const capture::SavedMarker& m : view.markers) {
      markers.push_back({m.id, m.sample, m.name});
    }
    if (auto restored = waveform_view::MarkerList::restored(markers)) {
      p.markers = std::move(*restored);
    } else {
      p.warnings.push_back(std::format("markers could not be restored ({}); none are shown", restored.error()));
    }
    if (view.cursors) {
      p.cursors = waveform_view::CursorPair(view.cursors->first, view.cursors->second);
      p.showCursors = view.cursors->shown;
    }
  }
  return p;
}

}

std::expected<std::vector<LoadedPanel>, std::string> panelsFromSession(hdf5::LoadedSession session) {
  try {
    std::vector<LoadedPanel> panels;
    for (std::size_t n = 0; n < session.devices.size(); ++n) {
      auto panel = panelOf(std::move(session.devices[n]), n);
      if (!panel) return std::unexpected(panel.error());
      panels.push_back(std::move(*panel));
    }
    return panels;
  } catch (const std::bad_alloc&) {
    return std::unexpected(std::string("out of memory while opening file"));
  }
}

namespace {

std::expected<std::vector<LoadedPanel>, std::string> loadPanels(const std::filesystem::path& path,
                                                                const hdf5::Hdf5ReaderOptions& options) {
  try {
    auto session = hdf5::loadSession(path, options);
    if (!session) return std::unexpected(session.error());
    return panelsFromSession(std::move(*session));
  } catch (const std::bad_alloc&) {
    return std::unexpected(std::string("out of memory while opening file"));
  } catch (const std::exception& e) {
    return std::unexpected(std::format("file could not be opened: {}", e.what()));
  }
}

}

std::expected<std::vector<LoadedPanel>, std::string> openSessionFile(const std::filesystem::path& path,
                                                                     const hdf5::Hdf5ReaderOptions& options) {
  auto panels = loadPanels(path, options);
  bitoscope::heap::giveFreeHeapBack();
  return panels;
}

}
