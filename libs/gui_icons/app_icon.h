/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <svg_raster.h>

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace bitoscope::gui_icons {

inline constexpr std::uint32_t kSmallAppIconMaxPixels = 24;

inline constexpr std::array<std::uint32_t, 6> kWindowIconSizes = {16, 32, 48, 64, 128, 256};

bool appIconUsesSmallVariant(std::uint32_t pixelSize);

struct AppIconSvgs {
  std::optional<std::string> full;
  std::optional<std::string> small;
};

struct AppIconSet {
  std::vector<svg_raster::RgbaImage> images;
  std::vector<std::string> problems;
};

AppIconSet rasterizeAppIcons(const AppIconSvgs& svgs, std::span<const std::uint32_t> sizes);

}
