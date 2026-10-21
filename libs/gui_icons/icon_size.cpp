/*
 * SPDX-License-Identifier: MIT
 */

#include "icon_size.h"

#include <algorithm>
#include <cmath>

namespace bitoscope::gui_icons {

std::uint32_t iconRasterPixels(std::uint32_t layoutPixels, float framebufferScale) {
  if (!std::isfinite(framebufferScale) || framebufferScale <= 0.0f || framebufferScale == 1.0f) {
    return layoutPixels;
  }
  const long pixels = std::lround(static_cast<double>(layoutPixels) * framebufferScale);
  return static_cast<std::uint32_t>((std::max)(1L, pixels));
}

}
