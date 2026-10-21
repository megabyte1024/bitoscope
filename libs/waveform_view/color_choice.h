/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

#include "channel_colors.h"

namespace bitoscope::waveform_view {

enum class ColorChoiceKind {
  Palette,
  Custom,
};

struct ColorChoice {
  ColorChoiceKind kind = ColorChoiceKind::Palette;

  std::uint32_t paletteIndex = 0;

  std::uint32_t argb = 0xFFFFFFFFu;

  static ColorChoice palette(std::uint32_t index);
  static ColorChoice custom(std::uint32_t argb);

  bool operator==(const ColorChoice&) const = default;
};

std::expected<std::uint32_t, std::string> parseArgb(std::string_view text);

std::string formatArgb(std::uint32_t argb);

Rgb8 resolveColor(const ColorChoice& choice, bool dark);

bool colorNeedsOutline(const ColorChoice& choice, bool dark);

Rgb8 outlineColorFor(const ColorChoice& choice, bool dark);

Rgb8 laneBackgroundFor(const ColorChoice& choice, bool dark);
bool laneNeedsOutlineFor(const ColorChoice& choice, bool dark);
Rgb8 laneOutlineColorFor(const ColorChoice& choice, bool dark);

Rgb8 unselectedMarkColorFor(const ColorChoice& choice, bool dark);

struct SelectorButtonColors {
  Rgb8 fill;
  bool outlined = false;
  Rgb8 outline;
  Rgb8 unselectedMark;
  Rgb8 selectedText;
  bool operator==(const SelectorButtonColors&) const = default;
};
SelectorButtonColors selectorButtonColors(const ColorChoice& choice, bool dark);

std::string toText(const ColorChoice& choice);
std::expected<ColorChoice, std::string> fromText(std::string_view text);

}
