/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <gui_locale.h>
#include <svg_raster.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace bitoscope::gui {

enum class Icon {
  About,
  Cursors,
  Decoders,
  Device,
  Log,
  Open,
  Pin,
  Properties,
  Save,
  Signal,
  Start,
  Stop,
  Theme,
  TriggerRising,
  TriggerHigh,
  TriggerFalling,
  TriggerLow,
  TriggerEdge,
  Zoom1to1,
  ZoomFit,
  ZoomIn,
  ZoomOut,
};
inline constexpr std::size_t kIconCount = 22;
static_assert(static_cast<std::size_t>(Icon::ZoomOut) + 1 == kIconCount);

std::string_view iconFileName(Icon icon);

struct IconAssets {
  std::array<std::string, kIconCount> svg;

  std::uint32_t pixelSize = 0;
  svg_raster::Rgba8 color{};
  std::array<std::optional<svg_raster::RgbaImage>, kIconCount> images;

  std::vector<std::string> problems;
};

IconAssets loadIcons(const std::filesystem::path& assetDir, std::uint32_t iconPixelSize,
                     svg_raster::Rgba8 iconColor);

inline constexpr std::string_view kFontFile = "fonts/google_sans_flex/GoogleSansFlexText[wght].ttf";

struct FontAsset {
  std::vector<std::uint8_t> bytes;
  std::optional<std::string> problem;
  std::string label;
};

FontAsset loadFontFile(const std::filesystem::path& fontPath, std::string_view label);

FontAsset loadFont(const std::filesystem::path& assetDir);

}
