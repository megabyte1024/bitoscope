/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <vector>

namespace bitoscope::png_write {

std::vector<std::uint8_t> encodePng(std::uint32_t width, std::uint32_t height,
                                    const std::vector<std::uint8_t>& pixels);

}
