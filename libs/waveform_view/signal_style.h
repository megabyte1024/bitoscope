/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <optional>

#include "channel_colors.h"
#include "color_choice.h"

namespace bitoscope::waveform_view {

enum class SignalStyle {
  LevelColors,
  ChannelColors,
};

struct FillTint {
  Rgb8 color;
  std::uint8_t alpha = 0;

  bool operator==(const FillTint&) const = default;
};

struct LaneColors {
  Rgb8 high;
  Rgb8 low;
  Rgb8 edge;
  Rgb8 dot;

  std::optional<FillTint> fill;

  bool outline = false;
  Rgb8 outlineColor{};

  bool operator==(const LaneColors&) const = default;
};

inline constexpr std::uint8_t kFillAlpha = 13;

LaneColors laneColors(SignalStyle style, std::uint32_t channel, bool dark);

LaneColors laneColors(SignalStyle style, const ColorChoice& choice, bool dark);

struct DotGeometry {
  float outerRadius;
  float innerRadius;
  bool ring = false;

  bool operator==(const DotGeometry&) const = default;
};

DotGeometry dotGeometry(bool needsOutline, float baseRadius, float lineHalfWidth,
                        float physicalPixel);

}
