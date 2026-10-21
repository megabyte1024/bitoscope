/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>

namespace bitoscope::gui_prefs {

inline constexpr int32_t kZoomMinPercent = 50;
inline constexpr int32_t kZoomMaxPercent = 300;
inline constexpr int32_t kZoomStepPercent = 10;
inline constexpr int32_t kZoomDefaultPercent = 100;

int32_t clampZoomPercent(int32_t percent);

int32_t zoomIn(int32_t current);

int32_t zoomOut(int32_t current);

float zoomFactor(int32_t percent);

}
