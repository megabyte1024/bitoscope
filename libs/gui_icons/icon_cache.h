/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <svg_raster.h>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <map>
#include <string>
#include <tuple>
#include <vector>

namespace bitoscope::gui_icons {

using TextureHandle = std::uint64_t;
inline constexpr TextureHandle kNoTexture = 0;

struct TextureCallbacks {
  std::function<std::expected<TextureHandle, std::string>(const svg_raster::RgbaImage&)> create;
  std::function<void(TextureHandle)> destroy;
};

enum class EntryState {
  NotCached,
  Ready,
  Failed,
};

class IconCache {
 public:
  IconCache(std::vector<std::string> iconNames, TextureCallbacks textures);
  ~IconCache();
  IconCache(const IconCache&) = delete;
  IconCache& operator=(const IconCache&) = delete;

  std::size_t iconCount() const { return names_.size(); }

  void setSources(std::vector<std::string> svg);
  bool sourcesArrived() const { return sourcesArrived_; }

  void adopt(std::size_t icon, std::uint32_t pixelSize, svg_raster::Rgba8 color,
             const svg_raster::RgbaImage& image);

  TextureHandle get(std::size_t icon, std::uint32_t pixelSize, svg_raster::Rgba8 color);

  EntryState state(std::size_t icon, std::uint32_t pixelSize, svg_raster::Rgba8 color) const;

  void clear();

  std::size_t textureCount() const;

  std::vector<std::string> takeProblems();

 private:
  using Key = std::tuple<std::size_t, std::uint32_t, std::uint32_t>;
  static Key keyOf(std::size_t icon, std::uint32_t pixelSize, svg_raster::Rgba8 color);
  TextureHandle make(std::size_t icon, const svg_raster::RgbaImage& image);

  std::vector<std::string> names_;
  TextureCallbacks textures_;
  std::vector<std::string> svg_;
  bool sourcesArrived_ = false;
  std::map<Key, TextureHandle> entries_;
  std::vector<std::string> problems_;
};

}
