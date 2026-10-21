/*
 * SPDX-License-Identifier: MIT
 */

#include "wayland_output.h"

#include <cmath>

namespace bitoscope::gui_linux {

std::optional<LogicalSize> waylandLogicalOutputSize(int32_t modeWidth, int32_t modeHeight,
                                                    float outputScale,
                                                    std::optional<float> windowScale) {
  if (modeWidth <= 0 || modeHeight <= 0) {
    return std::nullopt;
  }
  // WHY: Whole-number arithmetic from n = round(scale * 120): float division is a pixel off (1920 / 1.2f is
  // 1599.99...).
  const auto toNumerator = [](float scale) { return std::lround(static_cast<double>(scale) * 120.0); };
  int64_t numerator = toNumerator(outputScale);
  // WHY: A window not yet on an output reports scale 1, which must not be taken for the real scale.
  if (windowScale.has_value()) {
    const int64_t windowNumerator = toNumerator(*windowScale);
    if (windowNumerator > 0 && (windowNumerator + 119) / 120 * 120 == numerator) {
      numerator = windowNumerator;
    }
  }
  if (numerator <= 0) {
    return std::nullopt;
  }
  return LogicalSize{static_cast<int32_t>(int64_t{modeWidth} * 120 / numerator),
                     static_cast<int32_t>(int64_t{modeHeight} * 120 / numerator)};
}

}
