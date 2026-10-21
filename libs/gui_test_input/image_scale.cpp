/*
 * SPDX-License-Identifier: MIT
 */

#include "image_scale.h"

#include <cstddef>

namespace bitoscope::gui_test_input {

RgbaPicture boxDownscale(std::uint32_t width, std::uint32_t height, std::span<const std::uint8_t> rgba,
                         std::uint32_t factor) {
  constexpr std::size_t kChannels = 4;
  const std::size_t pixelCount = static_cast<std::size_t>(width) * height;
  if (width == 0 || height == 0 || rgba.size() < pixelCount * kChannels) {
    return RgbaPicture{};
  }
  RgbaPicture out;
  if (factor <= 1) {
    out.width = width;
    out.height = height;
    out.rgba.assign(rgba.begin(), rgba.begin() + static_cast<std::ptrdiff_t>(pixelCount * kChannels));
    return out;
  }
  out.width = (width + factor - 1) / factor;
  out.height = (height + factor - 1) / factor;
  out.rgba.resize(static_cast<std::size_t>(out.width) * out.height * kChannels);
  for (std::uint32_t oy = 0; oy < out.height; ++oy) {
    const std::uint32_t y0 = oy * factor;
    const std::uint32_t y1 = (y0 + factor < height) ? y0 + factor : height;
    for (std::uint32_t ox = 0; ox < out.width; ++ox) {
      const std::uint32_t x0 = ox * factor;
      const std::uint32_t x1 = (x0 + factor < width) ? x0 + factor : width;
      std::uint64_t sums[kChannels] = {0, 0, 0, 0};
      for (std::uint32_t y = y0; y < y1; ++y) {
        const std::uint8_t* row = rgba.data() + (static_cast<std::size_t>(y) * width + x0) * kChannels;
        for (std::uint32_t x = x0; x < x1; ++x) {
          for (std::size_t c = 0; c < kChannels; ++c) {
            sums[c] += row[(x - x0) * kChannels + c];
          }
        }
      }
      const std::uint64_t count = static_cast<std::uint64_t>(y1 - y0) * (x1 - x0);
      std::uint8_t* dst = out.rgba.data() + (static_cast<std::size_t>(oy) * out.width + ox) * kChannels;
      for (std::size_t c = 0; c < kChannels; ++c) {
        dst[c] = static_cast<std::uint8_t>((sums[c] * 2 + count) / (count * 2));
      }
    }
  }
  return out;
}

}
