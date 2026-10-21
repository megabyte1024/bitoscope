/*
 * SPDX-License-Identifier: MIT
 */

#include "pixel_rows.h"

#include <format>

namespace bitoscope::gui_test_input {

std::expected<RgbaPicture, std::string> opaqueRgbaFromRows(std::uint32_t width, std::uint32_t height,
                                                          std::size_t rowPitch, SourcePixelOrder order,
                                                          std::span<const std::uint8_t> bytes) {
  RgbaPicture picture;
  picture.width = width;
  picture.height = height;
  if (width == 0 || height == 0) {
    return picture;
  }
  const std::size_t rowBytes = static_cast<std::size_t>(width) * 4;
  if (rowPitch < rowBytes) {
    return std::unexpected(std::format("row pitch {} is shorter than a row of {} pixels ({} bytes)", rowPitch,
                                       width, rowBytes));
  }
  const std::size_t needed = rowPitch * (height - 1) + rowBytes;
  if (bytes.size() < needed) {
    return std::unexpected(
      std::format("{} x {} pixels need {} bytes, only {} were read", width, height, needed, bytes.size()));
  }
  const std::size_t red = order == SourcePixelOrder::Bgra ? 2 : 0;
  const std::size_t blue = order == SourcePixelOrder::Bgra ? 0 : 2;
  picture.rgba.resize(rowBytes * height);
  for (std::uint32_t y = 0; y < height; ++y) {
    const std::uint8_t* in = bytes.data() + rowPitch * y;
    std::uint8_t* out = picture.rgba.data() + rowBytes * y;
    for (std::uint32_t x = 0; x < width; ++x, in += 4, out += 4) {
      out[0] = in[red];
      out[1] = in[1];
      out[2] = in[blue];
      out[3] = 255;
    }
  }
  return picture;
}

}
