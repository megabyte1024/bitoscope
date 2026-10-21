/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "color_choice.h"
#include "ipc_la_acquisition.h"

namespace bitoscope::gui_capture {

std::vector<waveform_view::ColorChoice> defaultChannelColors(
  std::uint32_t channelCount, std::span<const ipc::la::ChannelPreset> presets);

bool canResetChannelColor(std::span<const waveform_view::ColorChoice> colors,
                          std::span<const waveform_view::ColorChoice> defaults, std::size_t channel);

bool resetChannelColor(std::vector<waveform_view::ColorChoice>& colors,
                       std::span<const waveform_view::ColorChoice> defaults, std::size_t channel);

waveform_view::ColorChoice channelColorOrPalette(std::span<const waveform_view::ColorChoice> colors,
                                                 std::size_t channel);

}
