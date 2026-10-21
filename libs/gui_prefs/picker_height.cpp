/*
 * SPDX-License-Identifier: MIT
 */

#include "picker_height.h"

#include <algorithm>
#include <cmath>

namespace bitoscope::gui_prefs {

int32_t clampPickerHeightCentiEm(int64_t centiEm) {
  return static_cast<int32_t>(
    std::clamp(centiEm, int64_t{kPickerHeightMinCentiEm}, int64_t{kPickerHeightMaxCentiEm}));
}

float pickerHeightPixels(int32_t centiEm, float fontSize) {
  return fontSize * static_cast<float>(clampPickerHeightCentiEm(centiEm)) / 100.0f;
}

int32_t pickerHeightFromPixels(float pixels, float fontSize) {
  if (!(fontSize > 0.0f)) return kPickerHeightDefaultCentiEm;
  const double centiEm = std::round(static_cast<double>(pixels) * 100.0 / static_cast<double>(fontSize));
  return clampPickerHeightCentiEm(static_cast<int64_t>(std::clamp(centiEm, -1.0e9, 1.0e9)));
}

}
