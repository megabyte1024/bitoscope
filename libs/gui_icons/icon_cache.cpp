/*
 * SPDX-License-Identifier: MIT
 */

#include "icon_cache.h"

#include <format>
#include <utility>

namespace bitoscope::gui_icons {

IconCache::IconCache(std::vector<std::string> iconNames, TextureCallbacks textures)
    : names_(std::move(iconNames)), textures_(std::move(textures)) {}

IconCache::~IconCache() {
  clear();
}

void IconCache::setSources(std::vector<std::string> svg) {
  svg.resize(names_.size());
  svg_ = std::move(svg);
  sourcesArrived_ = true;
}

IconCache::Key IconCache::keyOf(std::size_t icon, std::uint32_t pixelSize,
                                svg_raster::Rgba8 color) {
  const std::uint32_t packed = (std::uint32_t{color.r} << 24) | (std::uint32_t{color.g} << 16) |
                               (std::uint32_t{color.b} << 8) | std::uint32_t{color.a};
  return {icon, pixelSize, packed};
}

TextureHandle IconCache::make(std::size_t icon, const svg_raster::RgbaImage& image) {
  auto texture = textures_.create(image);
  if (!texture) {
    problems_.push_back(
      std::format("Icon {} texture could not be made: {}", names_[icon], texture.error()));
    return kNoTexture;
  }
  if (*texture == kNoTexture) {
    problems_.push_back(std::format("Icon {} texture could not be made: no handle returned",
                                    names_[icon]));
  }
  return *texture;
}

void IconCache::adopt(std::size_t icon, std::uint32_t pixelSize, svg_raster::Rgba8 color,
                      const svg_raster::RgbaImage& image) {
  if (icon >= names_.size()) {
    return;
  }
  const Key key = keyOf(icon, pixelSize, color);
  if (entries_.contains(key)) {
    return;
  }
  entries_[key] = make(icon, image);
}

TextureHandle IconCache::get(std::size_t icon, std::uint32_t pixelSize, svg_raster::Rgba8 color) {
  if (icon >= names_.size()) {
    return kNoTexture;
  }
  const Key key = keyOf(icon, pixelSize, color);
  if (auto it = entries_.find(key); it != entries_.end()) {
    return it->second;
  }
  // WHY: Cache nothing when the source is missing: a cached failure here once hid the icon after its source arrived.
  if (!sourcesArrived_ || svg_[icon].empty()) {
    return kNoTexture;
  }
  TextureHandle texture = kNoTexture;
  if (auto image = svg_raster::rasterize(svg_[icon], pixelSize, color)) {
    texture = make(icon, *image);
  } else {
    problems_.push_back(std::format("Icon {} could not be drawn at {} px: {}", names_[icon],
                                    pixelSize, svg_raster::describe(image.error())));
  }
  entries_[key] = texture;
  return texture;
}

EntryState IconCache::state(std::size_t icon, std::uint32_t pixelSize,
                            svg_raster::Rgba8 color) const {
  const auto it = entries_.find(keyOf(icon, pixelSize, color));
  if (it == entries_.end()) {
    return EntryState::NotCached;
  }
  return it->second == kNoTexture ? EntryState::Failed : EntryState::Ready;
}

void IconCache::clear() {
  for (const auto& [key, texture] : entries_) {
    if (texture != kNoTexture && textures_.destroy) {
      textures_.destroy(texture);
    }
  }
  entries_.clear();
}

std::size_t IconCache::textureCount() const {
  std::size_t count = 0;
  for (const auto& [key, texture] : entries_) {
    count += texture != kNoTexture ? 1 : 0;
  }
  return count;
}

std::vector<std::string> IconCache::takeProblems() {
  return std::exchange(problems_, {});
}

}
