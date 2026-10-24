/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "decoder_config.h"

namespace bitoscope::gui_decode {

std::vector<CapturedChannel> bindableChannels(std::span<const std::uint32_t> keptDeviceChannels,
                                              std::size_t deviceChannelCount,
                                              const std::vector<std::string>& presetNames);

void selectForNextCapture(std::vector<bool>& nextCaptureChannels, std::uint32_t deviceChannel);

}
