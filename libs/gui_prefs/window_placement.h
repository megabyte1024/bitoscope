/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>

#include "preferences.h"

namespace bitoscope::gui_prefs {

struct ScreenRect {
  int32_t left = 0;
  int32_t top = 0;
  int32_t right = 0;
  int32_t bottom = 0;
};

inline constexpr int32_t kTitleStripHeight = 32;
inline constexpr int32_t kMinVisibleWidth = 100;

bool placementIsVisible(const WindowPlacement& placement, std::span<const ScreenRect> workAreas);

struct WindowRectPlan {
  bool hasPosition = false;
  int32_t left = 0;
  int32_t top = 0;
  int32_t width = 0;
  int32_t height = 0;
};

WindowRectPlan windowRectPlan(const std::optional<WindowPlacement>& placement,
                              const std::optional<ScreenRect>& workArea, int32_t defaultWidth,
                              int32_t defaultHeight);

std::optional<std::string> placementRejectionNote(const WindowPlacement& placement,
                                                   std::span<const ScreenRect> workAreas);

}
