/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

// WHY: Render icon textures at framebuffer pixels, not layout units: an enlarged texture looks soft.

#include <cstdint>

namespace bitoscope::gui_icons {

std::uint32_t iconRasterPixels(std::uint32_t layoutPixels, float framebufferScale);

}
