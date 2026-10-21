/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>

namespace bitoscope::gui_prefs {

inline constexpr int32_t kLaneNameWidthDefaultCentiEm = 700;
inline constexpr int32_t kLaneNameWidthMinCentiEm = 700;
inline constexpr int32_t kLaneNameWidthMaxCentiEm = 6000;

int32_t clampLaneNameWidth(int64_t centiEm);

float laneNameWidthPixels(int32_t centiEm, float fontSize, float widestPixels);

int32_t laneNameWidthFromPixels(float pixels, float fontSize, float widestPixels);

int32_t laneNameWidthDragged(int32_t pressCentiEm, float fontSize, float pressPointerX, float pointerX,
                             float widestPixels);

}
