/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>

#include "image_scale.h"

namespace bitoscope::gui_test_input {

enum class SourcePixelOrder { Rgba, Bgra };

std::expected<RgbaPicture, std::string> opaqueRgbaFromRows(std::uint32_t width, std::uint32_t height,
                                                          std::size_t rowPitch, SourcePixelOrder order,
                                                          std::span<const std::uint8_t> bytes);

}
