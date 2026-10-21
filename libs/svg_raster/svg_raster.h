/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <expected>
#include <string_view>
#include <vector>

namespace bitoscope::svg_raster {

struct Rgba8 {
  std::uint8_t r = 0;
  std::uint8_t g = 0;
  std::uint8_t b = 0;
  std::uint8_t a = 255;

  bool operator==(const Rgba8&) const = default;
};

struct RgbaImage {
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::vector<std::uint8_t> pixels;
};

enum class RasterError {
  ZeroSize,
  TooLarge,
  DocumentTooLarge,
  Malformed,
  RenderFailed,
  OutOfMemory,
};

inline constexpr std::uint32_t kMaxPixelSize = 1024;

std::expected<RgbaImage, RasterError> rasterize(std::string_view svg, std::uint32_t pixelSize,
                                                Rgba8 currentColor);

std::string_view describe(RasterError error);

}
