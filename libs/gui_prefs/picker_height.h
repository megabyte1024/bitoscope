/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>

namespace bitoscope::gui_prefs {

inline constexpr int32_t kPickerHeightDefaultCentiEm = 1000;
inline constexpr int32_t kPickerHeightMinCentiEm = 500;
inline constexpr int32_t kPickerHeightMaxCentiEm = 6000;

int32_t clampPickerHeightCentiEm(int64_t centiEm);

float pickerHeightPixels(int32_t centiEm, float fontSize);

int32_t pickerHeightFromPixels(float pixels, float fontSize);

}
