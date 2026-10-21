/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace bitoscope::gui_test_input {

struct RgbaPicture {
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::vector<std::uint8_t> rgba;
  friend bool operator==(const RgbaPicture&, const RgbaPicture&) = default;
};

RgbaPicture boxDownscale(std::uint32_t width, std::uint32_t height, std::span<const std::uint8_t> rgba,
                         std::uint32_t factor);

}
