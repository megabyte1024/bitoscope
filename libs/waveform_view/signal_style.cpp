/*
 * SPDX-License-Identifier: MIT
 */

#include "signal_style.h"

#include <algorithm>

namespace bitoscope::waveform_view {

namespace {

constexpr Rgb8 kHighColor{0, 192, 0};
constexpr Rgb8 kLowColor{192, 0, 0};
constexpr Rgb8 kEdgeColor{128, 128, 128};
constexpr Rgb8 kDotColor{119, 119, 119};

double worstLaneContrast(Rgb8 colour, bool dark) {
  return (std::min)(contrastRatio(colour, traceBackground(dark)),
                    (std::min)(contrastRatio(colour, laneBackgroundAgainst(Rgb8{0, 0, 0}, dark)),
                               contrastRatio(colour, laneBackgroundAgainst(Rgb8{255, 255, 255}, dark))));
}

std::uint8_t scaled(std::uint8_t channel, int value, int peak) {
  return static_cast<std::uint8_t>((channel * value + peak / 2) / peak);
}

Rgb8 levelTone(Rgb8 base, bool dark) {
  if (worstLaneContrast(base, dark) >= kMinTraceContrast) {
    return base;
  }
  const int peak = (std::max)({int(base.r), int(base.g), int(base.b), 1});
  if (!dark) {
    for (int value = peak - 1; value >= 0; --value) {
      const Rgb8 candidate{scaled(base.r, value, peak), scaled(base.g, value, peak), scaled(base.b, value, peak)};
      if (worstLaneContrast(candidate, dark) >= kMinTraceContrast) return candidate;
    }
    return Rgb8{0, 0, 0};
  }
  for (int value = peak + 1; value <= 255; ++value) {
    const Rgb8 candidate{scaled(base.r, value, peak), scaled(base.g, value, peak), scaled(base.b, value, peak)};
    if (worstLaneContrast(candidate, dark) >= kMinTraceContrast) return candidate;
  }
  const Rgb8 full{scaled(base.r, 255, peak), scaled(base.g, 255, peak), scaled(base.b, 255, peak)};
  for (int step = 1; step <= 255; ++step) {
    const auto towardsWhite = [step](std::uint8_t c) {
      return static_cast<std::uint8_t>(c + ((255 - c) * step + 127) / 255);
    };
    const Rgb8 candidate{towardsWhite(full.r), towardsWhite(full.g), towardsWhite(full.b)};
    if (worstLaneContrast(candidate, dark) >= kMinTraceContrast) return candidate;
  }
  return Rgb8{255, 255, 255};
}

Rgb8 highColor(bool dark) {
  static const Rgb8 light = levelTone(kHighColor, false);
  static const Rgb8 darkTone = levelTone(kHighColor, true);
  return dark ? darkTone : light;
}

Rgb8 lowColor(bool dark) {
  static const Rgb8 light = levelTone(kLowColor, false);
  static const Rgb8 darkTone = levelTone(kLowColor, true);
  return dark ? darkTone : light;
}

constexpr Rgb8 kLightFillColor{0, 0, 0};
constexpr Rgb8 kDarkFillColor{255, 255, 255};

}

LaneColors laneColors(SignalStyle style, const ColorChoice& choice, bool dark) {
  if (style == SignalStyle::LevelColors) {
    LaneColors c;
    c.high = highColor(dark);
    c.low = lowColor(dark);
    c.edge = kEdgeColor;
    c.dot = kDotColor;
    c.fill = FillTint{dark ? kDarkFillColor : kLightFillColor, kFillAlpha};
    c.outline = false;
    c.outlineColor = Rgb8{};
    return c;
  }

  const Rgb8 own = resolveColor(choice, dark);
  LaneColors c;
  c.high = own;
  c.low = own;
  c.dot = own;
  c.fill = std::nullopt;
  c.outline = laneNeedsOutlineFor(choice, dark);
  c.outlineColor = laneOutlineColorFor(choice, dark);
  // WHY: Edge takes the outline colour: a condensed lane the colour of its background is unreadable.
  c.edge = c.outlineColor;
  return c;
}

LaneColors laneColors(SignalStyle style, std::uint32_t channel, bool dark) {
  return laneColors(style, ColorChoice::palette(channel), dark);
}

DotGeometry dotGeometry(bool needsOutline, float baseRadius, float lineHalfWidth,
                        float physicalPixel) {
  if (!needsOutline) {
    return DotGeometry{baseRadius, baseRadius, false};
  }
  const float ringWidth = physicalPixel;
  const float minOuterForVisibility = lineHalfWidth + ringWidth + physicalPixel;
  const float outer = (std::max)(baseRadius + ringWidth, minOuterForVisibility);
  return DotGeometry{outer, outer - ringWidth, true};
}

}
