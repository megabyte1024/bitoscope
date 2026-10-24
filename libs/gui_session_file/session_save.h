/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <chrono>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <capture_outcome.h>
#include <capture_writer.h>
#include <color_choice.h>
#include <cursors.h>
#include <ipc_messages.h>
#include <ipc_properties.h>
#include <lane_order.h>
#include <marker_list.h>

namespace bitoscope::gui_session_file {

struct SaveView {
  std::vector<std::uint32_t> deviceChannels;
  std::vector<std::string> channelNames;
  std::vector<waveform_view::ColorChoice> channelColors;
  std::vector<std::string> channelUserNames;
  waveform_view::MarkerList markers;
  std::optional<waveform_view::CursorPair> cursors;
  bool showCursors = false;
  std::optional<waveform_view::LaneOrder> laneOrder;
  std::vector<std::string> decoderIds;
  std::vector<capture::SavedProperty> deviceProperties;
  std::vector<std::pair<std::string, std::string>> decoderSetup;
  std::vector<std::string> decoderPresentation;
  std::vector<capture::DecoderVersions> decoderVersions;
};

struct DecoderSetupForSave {
  std::vector<std::pair<std::string, std::string>> setup;
  std::vector<std::string> presentation;
  std::vector<capture::DecoderVersions> versions;
};
DecoderSetupForSave decoderSetupForSave(std::span<const std::pair<std::string, std::string>> loadedSetup,
                                        std::span<const std::size_t> skipped,
                                        std::span<const std::pair<std::string, std::string>> currentSetup,
                                        std::span<const std::string> loadedPresentation = {},
                                        std::span<const std::string> currentPresentation = {},
                                        std::span<const std::optional<std::size_t>> unchanged = {},
                                        std::span<const capture::DecoderVersions> loadedVersions = {},
                                        std::span<const capture::DecoderVersions> currentVersions = {});

struct DecoderToSave {
  std::string name;
  std::string specText;
  std::optional<std::size_t> unchangedEntry;
  std::string presentation{};
  capture::DecoderVersions versions{};
};

struct DecoderUpdate {
  std::vector<std::pair<std::string, std::string>> setup;
  std::vector<std::string> presentation;
  std::vector<std::size_t> currentAt;
  std::vector<std::size_t> skippedAt;
  std::vector<capture::DecoderVersions> versions{};
};

DecoderUpdate decoderSetupForUpdate(std::span<const std::pair<std::string, std::string>> loadedSetup,
                                    std::span<const std::size_t> skipped, std::span<const DecoderToSave> current,
                                    std::span<const std::string> loadedPresentation = {},
                                    std::span<const capture::DecoderVersions> loadedVersions = {});

std::expected<void, std::string> updateSessionDecoders(const std::filesystem::path& path,
                                                        std::uint32_t deviceIndex, const DecoderUpdate& update);

std::expected<capture::CaptureMetadata, std::string> captureMetadataFor(const gui_capture::CaptureOutcome& kept,
                                                                        const SaveView& view);

std::vector<capture::SavedProperty> savedDeviceProperties(std::span<const ipc::props::PropertyValue> sent,
                                                          const ipc::DeviceInfo& device);

std::expected<void, std::string> writeSessionFile(const std::filesystem::path& path,
                                                  const gui_capture::CaptureOutcome& kept,
                                                  const capture::CaptureMetadata& metadata, std::uint32_t gzipLevel = 6);

std::expected<void, std::string> updateSessionView(const std::filesystem::path& path,
                                                    std::uint32_t deviceIndex, const SaveView& view);

struct CivilTime {
  int year = 1970;
  int month = 1;
  int day = 1;
  int hour = 0;
  int minute = 0;
  bool operator==(const CivilTime&) const = default;
};

CivilTime localCivilTime(std::chrono::system_clock::time_point when);

std::string suggestedFileName(std::string_view sessionName, const CivilTime& when);

}
