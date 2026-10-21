/*
 * SPDX-License-Identifier: MIT
 */

#include "zoom_level.h"

#include <algorithm>
#include <cstdint>

namespace bitoscope::gui_prefs {

int32_t clampZoomPercent(int32_t percent) {
  // WHY: Use 64-bit arithmetic: a corrupt value near INT32_MIN/MAX must not overflow.
  const int64_t clamped =
      (std::min)(int64_t{kZoomMaxPercent}, (std::max)(int64_t{kZoomMinPercent}, int64_t{percent}));
  const int64_t stepsFromMin =
      (clamped - kZoomMinPercent + kZoomStepPercent / 2) / kZoomStepPercent;
  const int64_t snapped = kZoomMinPercent + stepsFromMin * kZoomStepPercent;
  return static_cast<int32_t>(
      (std::min)(int64_t{kZoomMaxPercent}, (std::max)(int64_t{kZoomMinPercent}, snapped)));
}

int32_t zoomIn(int32_t current) {
  return clampZoomPercent(clampZoomPercent(current) + kZoomStepPercent);
}

int32_t zoomOut(int32_t current) {
  return clampZoomPercent(clampZoomPercent(current) - kZoomStepPercent);
}

float zoomFactor(int32_t percent) {
  return static_cast<float>(percent) / 100.0f;
}

}
