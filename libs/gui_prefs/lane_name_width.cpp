/*
 * SPDX-License-Identifier: MIT
 */

#include "lane_name_width.h"

#include <algorithm>
#include <cmath>

namespace bitoscope::gui_prefs {

int32_t clampLaneNameWidth(int64_t centiEm) {
  return static_cast<int32_t>(
    std::clamp(centiEm, int64_t{kLaneNameWidthMinCentiEm}, int64_t{kLaneNameWidthMaxCentiEm}));
}

float laneNameWidthPixels(int32_t centiEm, float fontSize, float widestPixels) {
  const float narrowest = fontSize * static_cast<float>(kLaneNameWidthMinCentiEm) / 100.0f;
  const float wanted = fontSize * static_cast<float>(clampLaneNameWidth(centiEm)) / 100.0f;
  return (std::max)(narrowest, (std::min)(wanted, widestPixels));
}

int32_t laneNameWidthFromPixels(float pixels, float fontSize, float widestPixels) {
  if (!(fontSize > 0.0f)) return kLaneNameWidthDefaultCentiEm;
  const float limited = (std::min)(pixels, (std::max)(widestPixels, 0.0f));
  const double centiEm = std::round(static_cast<double>(limited) * 100.0 / static_cast<double>(fontSize));
  return clampLaneNameWidth(static_cast<int64_t>(std::clamp(centiEm, -1.0e9, 1.0e9)));
}

int32_t laneNameWidthDragged(int32_t pressCentiEm, float fontSize, float pressPointerX, float pointerX,
                             float widestPixels) {
  if (!(fontSize > 0.0f)) return kLaneNameWidthDefaultCentiEm;
  const float travel = pointerX - pressPointerX;
  if (travel == 0.0f) return clampLaneNameWidth(pressCentiEm);
  // WHY: Start from the exact width shown at the press, not floored to whole pixels, so the first movement
  // does not jump.
  return laneNameWidthFromPixels(laneNameWidthPixels(pressCentiEm, fontSize, widestPixels) + travel, fontSize,
                                 widestPixels);
}

}
