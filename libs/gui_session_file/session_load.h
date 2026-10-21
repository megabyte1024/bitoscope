/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <capture_outcome.h>
#include <capture_writer.h>
#include <color_choice.h>
#include <cursors.h>
#include <hdf5_capture_reader.h>
#include <lane_order.h>
#include <marker_list.h>

namespace bitoscope::gui_session_file {

struct LoadedPanel {
  gui_capture::CaptureOutcome outcome;
  std::string device;
  std::uint64_t sampleRateMilliHz = 0;
  std::vector<std::uint32_t> deviceChannels;
  std::vector<std::string> channelNames;
  std::vector<std::string> channelUserNames;
  std::vector<waveform_view::ColorChoice> channelColors;
  waveform_view::MarkerList markers;
  std::optional<waveform_view::CursorPair> cursors;
  bool showCursors = false;
  std::optional<std::string> laneOrderText;
  std::optional<std::string> decoderVisibilityText;
  std::vector<capture::SavedProperty> deviceProperties;
  // WHY: Decoded results stored in a file are ignored: owner decision, every decoder runs again on open.
  std::vector<std::pair<std::string, std::string>> decoderSetup;
  std::vector<std::string> decoderPresentation;
  std::vector<capture::DecoderVersions> decoderVersions;
  std::vector<std::string> warnings;
};

std::expected<std::vector<LoadedPanel>, std::string> panelsFromSession(hdf5::LoadedSession session);

std::expected<std::vector<LoadedPanel>, std::string> openSessionFile(const std::filesystem::path& path,
                                                                     const hdf5::Hdf5ReaderOptions& options = {});

}
