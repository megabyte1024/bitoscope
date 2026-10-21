/*
 * SPDX-License-Identifier: MIT
 */

#include "default_channel_colors.h"

namespace bitoscope::gui_capture {

namespace wv = bitoscope::waveform_view;

std::vector<wv::ColorChoice> defaultChannelColors(std::uint32_t channelCount,
                                                  std::span<const ipc::la::ChannelPreset> presets) {
  std::vector<wv::ColorChoice> colors;
  colors.reserve(channelCount);
  for (std::uint32_t c = 0; c < channelCount; ++c) {
    colors.push_back(wv::ColorChoice::palette(c));
  }
  for (const ipc::la::ChannelPreset& preset : presets) {
    if (preset.channel >= channelCount) {
      continue;
    }
    if (auto color = wv::fromText(preset.colorId)) {
      colors[preset.channel] = *color;
    }
  }
  return colors;
}

bool canResetChannelColor(std::span<const wv::ColorChoice> colors, std::span<const wv::ColorChoice> defaults,
                          std::size_t channel) {
  return channel < colors.size() && channel < defaults.size() && !(colors[channel] == defaults[channel]);
}

bool resetChannelColor(std::vector<wv::ColorChoice>& colors, std::span<const wv::ColorChoice> defaults,
                       std::size_t channel) {
  if (!canResetChannelColor(colors, defaults, channel)) {
    return false;
  }
  colors[channel] = defaults[channel];
  return true;
}

wv::ColorChoice channelColorOrPalette(std::span<const wv::ColorChoice> colors, std::size_t channel) {
  return channel < colors.size() ? colors[channel] : wv::ColorChoice::palette(static_cast<std::uint32_t>(channel));
}

}
