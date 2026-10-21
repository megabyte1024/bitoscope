/*
 * SPDX-License-Identifier: MIT
 */

#include "color_choice.h"

#include <cmath>

namespace bitoscope::waveform_view {

namespace {

std::uint8_t mix8(std::uint8_t from, std::uint8_t to, double t) {
  return static_cast<std::uint8_t>(std::lround(from + (static_cast<double>(to) - from) * t));
}

int hexNibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

std::uint8_t argbAlpha(std::uint32_t argb) { return static_cast<std::uint8_t>((argb >> 24) & 0xFF); }
std::uint8_t argbRed(std::uint32_t argb) { return static_cast<std::uint8_t>((argb >> 16) & 0xFF); }
std::uint8_t argbGreen(std::uint32_t argb) { return static_cast<std::uint8_t>((argb >> 8) & 0xFF); }
std::uint8_t argbBlue(std::uint32_t argb) { return static_cast<std::uint8_t>(argb & 0xFF); }

}

ColorChoice ColorChoice::palette(std::uint32_t index) {
  ColorChoice c;
  c.kind = ColorChoiceKind::Palette;
  c.paletteIndex = index;
  return c;
}

ColorChoice ColorChoice::custom(std::uint32_t argb) {
  ColorChoice c;
  c.kind = ColorChoiceKind::Custom;
  c.argb = argb;
  return c;
}

std::expected<std::uint32_t, std::string> parseArgb(std::string_view text) {
  if (text.size() != 9 || text.front() != '#') {
    return std::unexpected(
      std::string("ARGB: must be \"#AARRGGBB\" ('#' then 8 hex digits)"));
  }
  std::uint32_t value = 0;
  for (std::size_t i = 1; i < text.size(); ++i) {
    const int nibble = hexNibble(text[i]);
    if (nibble < 0) {
      return std::unexpected(
        std::string("ARGB: not hex digit: '") + text[i] + "'");
    }
    value = (value << 4) | static_cast<std::uint32_t>(nibble);
  }
  return value;
}

std::string formatArgb(std::uint32_t argb) {
  static constexpr char kHex[] = "0123456789ABCDEF";
  std::string out(9, '#');
  for (int i = 0; i < 8; ++i) {
    const int shift = 28 - i * 4;
    out[1 + static_cast<std::size_t>(i)] = kHex[(argb >> shift) & 0xF];
  }
  return out;
}

Rgb8 resolveColor(const ColorChoice& choice, bool dark) {
  if (choice.kind == ColorChoiceKind::Palette) {
    return channelColor(choice.paletteIndex, dark);
  }
  const Rgb8 background = traceBackground(dark);
  const double t = argbAlpha(choice.argb) / 255.0;
  return Rgb8{mix8(background.r, argbRed(choice.argb), t),
             mix8(background.g, argbGreen(choice.argb), t),
             mix8(background.b, argbBlue(choice.argb), t)};
}

bool colorNeedsOutline(const ColorChoice& choice, bool dark) {
  if (choice.kind == ColorChoiceKind::Palette) {
    return channelNeedsOutline(choice.paletteIndex, dark);
  }
  return needsOutlineAgainst(resolveColor(choice, dark), traceBackground(dark));
}

Rgb8 outlineColorFor(const ColorChoice& choice, bool dark) {
  if (choice.kind == ColorChoiceKind::Palette) {
    return outlineColor(choice.paletteIndex, dark);
  }
  return outlineColorAgainst(resolveColor(choice, dark), traceBackground(dark), dark);
}

Rgb8 laneBackgroundFor(const ColorChoice& choice, bool dark) {
  if (choice.kind == ColorChoiceKind::Palette) {
    return laneBackground(choice.paletteIndex, dark);
  }
  return laneBackgroundAgainst(resolveColor(choice, dark), dark);
}

bool laneNeedsOutlineFor(const ColorChoice& choice, bool dark) {
  if (choice.kind == ColorChoiceKind::Palette) {
    return laneNeedsOutline(choice.paletteIndex, dark);
  }
  const Rgb8 own = resolveColor(choice, dark);
  return needsOutlineAgainst(own, laneBackgroundAgainst(own, dark));
}

Rgb8 unselectedMarkColorFor(const ColorChoice& choice, bool dark) {
  return colorNeedsOutline(choice, dark) ? outlineColorFor(choice, dark) : resolveColor(choice, dark);
}

SelectorButtonColors selectorButtonColors(const ColorChoice& choice, bool dark) {
  SelectorButtonColors b;
  b.fill = resolveColor(choice, dark);
  b.outlined = colorNeedsOutline(choice, dark);
  b.outline = outlineColorFor(choice, dark);
  b.unselectedMark = unselectedMarkColorFor(choice, dark);
  b.selectedText = textColorOn(b.fill);
  return b;
}

std::string toText(const ColorChoice& choice) {
  if (choice.kind == ColorChoiceKind::Palette) {
    return std::string(paletteIdentifier(choice.paletteIndex));
  }
  return formatArgb(choice.argb);
}

std::expected<ColorChoice, std::string> fromText(std::string_view text) {
  if (!text.empty() && text.front() == '#') {
    const auto argb = parseArgb(text);
    if (!argb.has_value()) {
      return std::unexpected(argb.error());
    }
    return ColorChoice::custom(*argb);
  }
  const auto index = paletteIndexForId(text);
  if (!index.has_value()) {
    return std::unexpected(
      std::string("ColorChoice: not palette id (\"color-1\"..\"color-32\") or \"#AARRGGBB\": '") +
      std::string(text) + "'");
  }
  return ColorChoice::palette(*index);
}

Rgb8 laneOutlineColorFor(const ColorChoice& choice, bool dark) {
  if (choice.kind == ColorChoiceKind::Palette) {
    return laneOutlineColor(choice.paletteIndex, dark);
  }
  const Rgb8 own = resolveColor(choice, dark);
  return outlineColorAgainst(own, laneBackgroundAgainst(own, dark), dark);
}

}
