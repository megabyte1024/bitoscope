/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace bitoscope::waveform_view {

struct Rgb8 {
  uint8_t r = 0;
  uint8_t g = 0;
  uint8_t b = 0;
  bool operator==(const Rgb8&) const = default;
};

inline constexpr uint32_t kChannelPaletteSize = 32;

inline constexpr double kMinTraceContrast = 3.0;

// WHY: kMinCableContrast is below kMinTraceContrast: saturated cable colours are told by hue, and a 3:1 floor
// forced tones too dark.
inline constexpr double kMinCableContrast = 2.0;

inline constexpr double kMinTextContrast = 4.5;

Rgb8 traceBackground(bool dark);

Rgb8 channelColor(uint32_t channel, bool dark);

Rgb8 markerColor(bool dark);

double contrastRatio(Rgb8 a, Rgb8 b);

std::string_view paletteIdentifier(uint32_t paletteIndex);

std::optional<uint32_t> paletteIndexForId(std::string_view id);

std::string_view paletteName(uint32_t paletteIndex);

bool channelNeedsOutline(uint32_t channel, bool dark);

Rgb8 outlineColor(uint32_t channel, bool dark);

Rgb8 textColorOn(Rgb8 fill);

Rgb8 unselectedMarkColor(uint32_t channel, bool dark);

Rgb8 laneBackground(uint32_t channel, bool dark);

bool laneNeedsOutline(uint32_t channel, bool dark);

bool needsOutlineAgainst(Rgb8 base, Rgb8 background);

Rgb8 outlineColorAgainst(Rgb8 base, Rgb8 background, bool dark);

Rgb8 laneBackgroundAgainst(Rgb8 base, bool dark);

Rgb8 laneOutlineColor(uint32_t channel, bool dark);

}
