/*
 * SPDX-License-Identifier: MIT
 */

#include "channel_choices.h"

#include "capture_settings.h"

namespace bitoscope::gui_decode {

std::vector<CapturedChannel> bindableChannels(std::span<const std::uint32_t> keptDeviceChannels,
                                              std::size_t deviceChannelCount,
                                              const std::vector<std::string>& presetNames) {
  std::vector<CapturedChannel> out;
  const auto add = [&](std::uint32_t device) {
    // WHY: Use the channel's OWN name, not the user's: a decoder setup from a file may bind a channel by it.
    out.push_back({device, gui_capture::channelDisplayName(presetNames, device)});
  };
  if (!keptDeviceChannels.empty()) {
    out.reserve(keptDeviceChannels.size());
    for (const std::uint32_t device : keptDeviceChannels) add(device);
  } else {
    out.reserve(deviceChannelCount);
    for (std::size_t device = 0; device < deviceChannelCount; ++device) add(static_cast<std::uint32_t>(device));
  }
  return out;
}

void selectForNextCapture(std::vector<bool>& nextCaptureChannels, std::uint32_t deviceChannel) {
  if (deviceChannel < nextCaptureChannels.size()) nextCaptureChannels[deviceChannel] = true;
}

}
