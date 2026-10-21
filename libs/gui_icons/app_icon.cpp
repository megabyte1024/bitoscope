/*
 * SPDX-License-Identifier: MIT
 */

#include "app_icon.h"

#include <format>

namespace bitoscope::gui_icons {

bool appIconUsesSmallVariant(std::uint32_t pixelSize) {
  return pixelSize <= kSmallAppIconMaxPixels;
}

AppIconSet rasterizeAppIcons(const AppIconSvgs& svgs, std::span<const std::uint32_t> sizes) {
  AppIconSet set;
  constexpr svg_raster::Rgba8 kUnusedCurrentColor{0, 0, 0, 255};
  for (const std::uint32_t size : sizes) {
    const bool small = appIconUsesSmallVariant(size);
    const std::optional<std::string>& wanted = small ? svgs.small : svgs.full;
    const std::optional<std::string>& other = small ? svgs.full : svgs.small;
    const std::optional<std::string>& use = wanted ? wanted : other;
    if (!use) {
      set.problems.push_back(std::format("Application icon not drawn at {} px: no SVG document", size));
      continue;
    }
    auto image = svg_raster::rasterize(*use, size, kUnusedCurrentColor);
    if (!image) {
      set.problems.push_back(std::format("Application icon not drawn at {} px: {}", size,
                                         svg_raster::describe(image.error())));
      continue;
    }
    set.images.push_back(std::move(*image));
  }
  return set;
}

}
