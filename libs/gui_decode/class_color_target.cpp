/*
 * SPDX-License-Identifier: MIT
 */

#include "class_color_target.h"

#include "decoder_config.h"

namespace bitoscope::gui_decode {

std::optional<waveform_view::ColorChoice> targetClassColor(std::span<const DecoderConfig> configs,
                                                           const ClassColorTarget& target) {
  for (const DecoderConfig& config : configs) {
    if (config.id() == target.decoderId) return config.classColor(target.row, target.cls);
  }
  return std::nullopt;
}

bool setTargetClassColor(std::span<DecoderConfig> configs, const ClassColorTarget& target,
                         std::optional<waveform_view::ColorChoice> color) {
  for (DecoderConfig& config : configs) {
    if (config.id() != target.decoderId) continue;
    if (config.classColor(target.row, target.cls) == color) return false;
    config.setClassColor(target.row, target.cls, color);
    return true;
  }
  return false;
}

}
