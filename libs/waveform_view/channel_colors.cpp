/*
 * SPDX-License-Identifier: MIT
 */

#include "channel_colors.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <string>

namespace bitoscope::waveform_view {

namespace {

double linear(uint8_t v) {
  const double c = v / 255.0;
  return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
}

double luminance(Rgb8 c) { return 0.2126 * linear(c.r) + 0.7152 * linear(c.g) + 0.0722 * linear(c.b); }

uint8_t mix(uint8_t from, uint8_t to, double t) {
  return static_cast<uint8_t>(std::lround(from + (to - from) * t));
}

constexpr int kSteps = 20;

Rgb8 movedForContrast(Rgb8 base, Rgb8 background, uint8_t target, double minContrast) {
  for (int step = 0; step <= kSteps; ++step) {
    const double t = static_cast<double>(step) / kSteps;
    const Rgb8 moved{mix(base.r, target, t), mix(base.g, target, t), mix(base.b, target, t)};
    if (contrastRatio(moved, background) >= minContrast) return moved;
  }
  return Rgb8{target, target, target};
}

Rgb8 adjustForContrast(Rgb8 base, bool dark) {
  return movedForContrast(base, traceBackground(dark), dark ? 255 : 0, kMinTraceContrast);
}

// WHY: Dark lane tint is half the light one: Dark-X tones sit at the contrast floor and more tint pushes them
// under it.
constexpr double kLightLaneTintStrength = 0.10;
constexpr double kDarkLaneTintStrength = 0.05;

constexpr Rgb8 kMarkerBaseColor{0xD1, 0x00, 0x8F};

// BEGIN GENERATED CHANNEL PALETTE (gen_channel_palette.py) - do not edit by hand
inline constexpr std::array<Rgb8, 32> kDarkChannelPalette{{
  {0x58, 0x58, 0x58},  // channel 0: Black
  {0x8A, 0x36, 0x24},  // channel 1: Brown
  {0xD2, 0x15, 0x24},  // channel 2: Red
  {0xDE, 0x58, 0x09},  // channel 3: Orange
  {0xDB, 0xC4, 0x00},  // channel 4: Yellow
  {0x00, 0x8F, 0x68},  // channel 5: Green
  {0x27, 0x4D, 0xC7},  // channel 6: Blue
  {0x70, 0x35, 0xA8},  // channel 7: Violet
  {0x7C, 0x7C, 0x7C},  // channel 8: Grey
  {0xE3, 0xE3, 0xE3},  // channel 9: White
  {0x75, 0x75, 0x75},  // channel 10: Light black
  {0xBE, 0x66, 0x51},  // channel 11: Light brown
  {0xFF, 0x6A, 0x63},  // channel 12: Light red
  {0xFF, 0x9D, 0x74},  // channel 13: Light orange
  {0xFF, 0xEF, 0x8C},  // channel 14: Light yellow
  {0x45, 0xC3, 0x96},  // channel 15: Light green
  {0x52, 0x80, 0xFE},  // channel 16: Light blue
  {0x9F, 0x67, 0xDD},  // channel 17: Light violet
  {0xAD, 0xAD, 0xAD},  // channel 18: Light grey
  {0xED, 0xED, 0xED},  // channel 19: Light white
  {0x4B, 0x4B, 0x4A},  // channel 20: Dark black
  {0x82, 0x30, 0x1D},  // channel 21: Dark brown
  {0x9B, 0x00, 0x13},  // channel 22: Dark red
  {0x96, 0x37, 0x00},  // channel 23: Dark orange
  {0xA4, 0x91, 0x00},  // channel 24: Dark yellow
  {0x00, 0x5A, 0x40},  // channel 25: Dark green
  {0x1B, 0x3D, 0xB6},  // channel 26: Dark blue
  {0x66, 0x2B, 0x9D},  // channel 27: Dark violet
  {0x4F, 0x4F, 0x4F},  // channel 28: Dark grey
  {0xB0, 0xB0, 0xB0},  // channel 29: Dark white
  {0x66, 0x66, 0x66},  // channel 30: Pale black
  {0xA4, 0x4E, 0x3A},  // channel 31: Pale brown
}};

inline constexpr std::array<Rgb8, 32> kLightChannelPalette{{
  {0x1E, 0x1E, 0x1E},  // channel 0: Black
  {0x9A, 0x45, 0x32},  // channel 1: Brown
  {0xE4, 0x30, 0x34},  // channel 2: Red
  {0xF0, 0x68, 0x26},  // channel 3: Orange
  {0xEC, 0xD4, 0x2A},  // channel 4: Yellow
  {0x05, 0xA0, 0x75},  // channel 5: Green
  {0x34, 0x5D, 0xD8},  // channel 6: Blue
  {0x7E, 0x45, 0xB8},  // channel 7: Violet
  {0x8B, 0x8B, 0x8B},  // channel 8: Grey
  {0xF4, 0xF4, 0xF4},  // channel 9: White
  {0x46, 0x46, 0x46},  // channel 10: Light black
  {0xCF, 0x75, 0x60},  // channel 11: Light brown
  {0xFF, 0x8A, 0x81},  // channel 12: Light red
  {0xFF, 0xB6, 0x98},  // channel 13: Light orange
  {0xFF, 0xFE, 0xF5},  // channel 14: Light yellow
  {0x58, 0xD3, 0xA5},  // channel 15: Light green
  {0x69, 0x93, 0xFF},  // channel 16: Light blue
  {0xAE, 0x77, 0xEE},  // channel 17: Light violet
  {0xBC, 0xBC, 0xBC},  // channel 18: Light grey
  {0xFD, 0xFD, 0xFD},  // channel 19: Light white
  {0x01, 0x01, 0x01},  // channel 20: Dark black
  {0x67, 0x15, 0x02},  // channel 21: Dark brown
  {0x9E, 0x00, 0x14},  // channel 22: Dark red
  {0xAD, 0x41, 0x00},  // channel 23: Dark orange
  {0xB5, 0xA1, 0x00},  // channel 24: Dark yellow
  {0x00, 0x6A, 0x4C},  // channel 25: Dark green
  {0x0F, 0x28, 0xA2},  // channel 26: Dark blue
  {0x51, 0x0B, 0x84},  // channel 27: Dark violet
  {0x5D, 0x5D, 0x5D},  // channel 28: Dark grey
  {0xC0, 0xC0, 0xC0},  // channel 29: Dark white
  {0x31, 0x31, 0x31},  // channel 30: Pale black
  {0xB4, 0x5D, 0x49},  // channel 31: Pale brown
}};

inline constexpr std::array<std::string_view, 32> kPaletteNames{{
  "Black",
  "Brown",
  "Red",
  "Orange",
  "Yellow",
  "Green",
  "Blue",
  "Violet",
  "Grey",
  "White",
  "Light black",
  "Light brown",
  "Light red",
  "Light orange",
  "Light yellow",
  "Light green",
  "Light blue",
  "Light violet",
  "Light grey",
  "Light white",
  "Dark black",
  "Dark brown",
  "Dark red",
  "Dark orange",
  "Dark yellow",
  "Dark green",
  "Dark blue",
  "Dark violet",
  "Dark grey",
  "Dark white",
  "Pale black",
  "Pale brown",
}};

inline constexpr std::array<bool, 32> kLightNeedsOutline{{
  false,  // channel 0: Black
  false,  // channel 1: Brown
  false,  // channel 2: Red
  false,  // channel 3: Orange
  true,  // channel 4: Yellow
  false,  // channel 5: Green
  false,  // channel 6: Blue
  false,  // channel 7: Violet
  false,  // channel 8: Grey
  true,  // channel 9: White
  false,  // channel 10: Light black
  false,  // channel 11: Light brown
  false,  // channel 12: Light red
  true,  // channel 13: Light orange
  true,  // channel 14: Light yellow
  true,  // channel 15: Light green
  false,  // channel 16: Light blue
  false,  // channel 17: Light violet
  true,  // channel 18: Light grey
  true,  // channel 19: Light white
  false,  // channel 20: Dark black
  false,  // channel 21: Dark brown
  false,  // channel 22: Dark red
  false,  // channel 23: Dark orange
  false,  // channel 24: Dark yellow
  false,  // channel 25: Dark green
  false,  // channel 26: Dark blue
  false,  // channel 27: Dark violet
  false,  // channel 28: Dark grey
  true,  // channel 29: Dark white
  false,  // channel 30: Pale black
  false,  // channel 31: Pale brown
}};

inline constexpr std::array<Rgb8, 32> kLightOutlineColors{{
  {0x1E, 0x1E, 0x1E},  // channel 0: Black
  {0x9A, 0x45, 0x32},  // channel 1: Brown
  {0xE4, 0x30, 0x34},  // channel 2: Red
  {0xF0, 0x68, 0x26},  // channel 3: Orange
  {0xBD, 0xA8, 0x00},  // channel 4: Yellow
  {0x05, 0xA0, 0x75},  // channel 5: Green
  {0x34, 0x5D, 0xD8},  // channel 6: Blue
  {0x7E, 0x45, 0xB8},  // channel 7: Violet
  {0x8B, 0x8B, 0x8B},  // channel 8: Grey
  {0xA8, 0xA8, 0xA8},  // channel 9: White
  {0x46, 0x46, 0x46},  // channel 10: Light black
  {0xCF, 0x75, 0x60},  // channel 11: Light brown
  {0xFF, 0x8A, 0x81},  // channel 12: Light red
  {0xFF, 0x84, 0x4F},  // channel 13: Light orange
  {0xBD, 0xA8, 0x00},  // channel 14: Light yellow
  {0x3C, 0xBC, 0x90},  // channel 15: Light green
  {0x69, 0x93, 0xFF},  // channel 16: Light blue
  {0xAE, 0x77, 0xEE},  // channel 17: Light violet
  {0xA8, 0xA8, 0xA8},  // channel 18: Light grey
  {0xA8, 0xA8, 0xA8},  // channel 19: Light white
  {0x01, 0x01, 0x01},  // channel 20: Dark black
  {0x67, 0x15, 0x02},  // channel 21: Dark brown
  {0x9E, 0x00, 0x14},  // channel 22: Dark red
  {0xAD, 0x41, 0x00},  // channel 23: Dark orange
  {0xB5, 0xA1, 0x00},  // channel 24: Dark yellow
  {0x00, 0x6A, 0x4C},  // channel 25: Dark green
  {0x0F, 0x28, 0xA2},  // channel 26: Dark blue
  {0x51, 0x0B, 0x84},  // channel 27: Dark violet
  {0x5D, 0x5D, 0x5D},  // channel 28: Dark grey
  {0xA8, 0xA8, 0xA8},  // channel 29: Dark white
  {0x31, 0x31, 0x31},  // channel 30: Pale black
  {0xB4, 0x5D, 0x49},  // channel 31: Pale brown
}};
// END GENERATED CHANNEL PALETTE

constexpr std::string_view kIdentifierPrefix = "color-";

}

Rgb8 traceBackground(bool dark) { return dark ? Rgb8{24, 25, 27} : Rgb8{255, 255, 255}; }

double contrastRatio(Rgb8 a, Rgb8 b) {
  const double la = luminance(a);
  const double lb = luminance(b);
  return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

Rgb8 channelColor(uint32_t channel, bool dark) {
  const std::array<Rgb8, kChannelPaletteSize>& table = dark ? kDarkChannelPalette : kLightChannelPalette;
  return table[channel % kChannelPaletteSize];
}

Rgb8 markerColor(bool dark) { return adjustForContrast(kMarkerBaseColor, dark); }

std::string_view paletteIdentifier(uint32_t paletteIndex) {
  static const std::array<std::string, kChannelPaletteSize> ids = [] {
    std::array<std::string, kChannelPaletteSize> out;
    for (uint32_t i = 0; i < kChannelPaletteSize; ++i) {
      out[i] = std::string(kIdentifierPrefix) + std::to_string(i + 1);
    }
    return out;
  }();
  return ids[paletteIndex % kChannelPaletteSize];
}

std::string_view paletteName(uint32_t paletteIndex) {
  return kPaletteNames[paletteIndex % kChannelPaletteSize];
}

bool channelNeedsOutline(uint32_t channel, bool dark) {
  // WHY: Dark theme never outlines: lifting a too-dark colour keeps its identity, deepening a too-light one erodes
  // it.
  if (dark) return false;
  return kLightNeedsOutline[channel % kChannelPaletteSize];
}

Rgb8 outlineColor(uint32_t channel, bool dark) {
  if (dark) return channelColor(channel, dark);
  return kLightOutlineColors[channel % kChannelPaletteSize];
}

Rgb8 textColorOn(Rgb8 fill) {
  constexpr Rgb8 kDarkText{0, 0, 0};
  constexpr Rgb8 kWhiteText{255, 255, 255};
  return contrastRatio(fill, kWhiteText) >= contrastRatio(fill, kDarkText) ? kWhiteText : kDarkText;
}

Rgb8 unselectedMarkColor(uint32_t channel, bool dark) {
  return channelNeedsOutline(channel, dark) ? outlineColor(channel, dark) : channelColor(channel, dark);
}

bool needsOutlineAgainst(Rgb8 base, Rgb8 background) {
  return contrastRatio(base, background) < kMinCableContrast;
}

Rgb8 outlineColorAgainst(Rgb8 base, Rgb8 background, bool dark) {
  if (!needsOutlineAgainst(base, background)) return base;
  return movedForContrast(base, background, dark ? 255 : 0, kMinCableContrast);
}

Rgb8 laneBackgroundAgainst(Rgb8 base, bool dark) {
  const Rgb8 background = traceBackground(dark);
  const double strength = dark ? kDarkLaneTintStrength : kLightLaneTintStrength;
  return Rgb8{mix(background.r, base.r, strength), mix(background.g, base.g, strength),
             mix(background.b, base.b, strength)};
}

Rgb8 laneBackground(uint32_t channel, bool dark) {
  return laneBackgroundAgainst(channelColor(channel, dark), dark);
}

bool laneNeedsOutline(uint32_t channel, bool dark) {
  return needsOutlineAgainst(channelColor(channel, dark), laneBackground(channel, dark));
}

Rgb8 laneOutlineColor(uint32_t channel, bool dark) {
  return outlineColorAgainst(channelColor(channel, dark), laneBackground(channel, dark), dark);
}

std::optional<uint32_t> paletteIndexForId(std::string_view id) {
  if (!id.starts_with(kIdentifierPrefix)) return std::nullopt;
  const std::string_view digits = id.substr(kIdentifierPrefix.size());
  if (digits.empty() || digits.front() == '0' || !std::isdigit(static_cast<unsigned char>(digits.front()))) {
    return std::nullopt;
  }
  uint32_t value = 0;
  const auto result = std::from_chars(digits.data(), digits.data() + digits.size(), value);
  if (result.ec != std::errc{} || result.ptr != digits.data() + digits.size()) return std::nullopt;
  if (value < 1 || value > kChannelPaletteSize) return std::nullopt;
  return value - 1;
}

}
