/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>

#include "color_choice.h"

namespace bitoscope::gui_decode {

class DecoderConfig;

struct ClassColorTarget {
  std::string decoderId;
  std::uint32_t row = 0;
  std::uint32_t cls = 0;
  bool operator==(const ClassColorTarget&) const = default;
};

std::optional<waveform_view::ColorChoice> targetClassColor(std::span<const DecoderConfig> configs,
                                                           const ClassColorTarget& target);

bool setTargetClassColor(std::span<DecoderConfig> configs, const ClassColorTarget& target,
                         std::optional<waveform_view::ColorChoice> color);

}
