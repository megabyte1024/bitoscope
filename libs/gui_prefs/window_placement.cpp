/*
 * SPDX-License-Identifier: MIT
 */

#include "window_placement.h"

#include <algorithm>
#include <format>

namespace bitoscope::gui_prefs {

bool placementIsVisible(const WindowPlacement& placement, std::span<const ScreenRect> workAreas) {
  if (!placement.position.has_value()) {
    return false;
  }
  // WHY: Use 64-bit arithmetic: a corrupt placement near INT32_MAX must not overflow.
  const int64_t left = placement.position->x;
  const int64_t top = placement.position->y;
  const int64_t right = left + placement.width;
  const int64_t stripBottom = top + (std::min)(int64_t{placement.height}, int64_t{kTitleStripHeight});
  const int64_t needWidth = (std::min)(int64_t{placement.width}, int64_t{kMinVisibleWidth});
  for (const ScreenRect& area : workAreas) {
    if (top < area.top || stripBottom > area.bottom) {
      continue;
    }
    const int64_t shownLeft = (std::max)(left, int64_t{area.left});
    const int64_t shownRight = (std::min)(right, int64_t{area.right});
    if (shownRight - shownLeft >= needWidth) {
      return true;
    }
  }
  return false;
}

WindowRectPlan windowRectPlan(const std::optional<WindowPlacement>& placement,
                              const std::optional<ScreenRect>& workArea, int32_t defaultWidth,
                              int32_t defaultHeight) {
  if (placement.has_value() && placement->position.has_value()) {
    return WindowRectPlan{.hasPosition = true,
                          .left = placement->position->x,
                          .top = placement->position->y,
                          .width = placement->width,
                          .height = placement->height};
  }

  int32_t width = placement.has_value() ? placement->width : defaultWidth;
  int32_t height = placement.has_value() ? placement->height : defaultHeight;
  int32_t left = 0;
  int32_t top = 0;
  if (workArea.has_value()) {
    const int32_t areaWidth = workArea->right - workArea->left;
    const int32_t areaHeight = workArea->bottom - workArea->top;
    width = (std::min)(width, areaWidth);
    height = (std::min)(height, areaHeight);
    left = workArea->left + (areaWidth - width) / 2;
    top = workArea->top + (areaHeight - height) / 2;
  }
  return WindowRectPlan{
    .hasPosition = false, .left = left, .top = top, .width = width, .height = height};
}

std::optional<std::string> placementRejectionNote(const WindowPlacement& placement,
                                                   std::span<const ScreenRect> workAreas) {
  if (!placement.position.has_value()) {
    // WHY: A placement without a position is never rejected: there is nothing to check and its saved size is still used.
    return std::nullopt;
  }
  if (placementIsVisible(placement, workAreas)) {
    return std::nullopt;
  }
  return std::format(
    "Saved window position ({}, {}, {} x {}) is on no monitor present now: window opens "
    "at its default place",
    placement.position->x, placement.position->y, placement.width, placement.height);
}

}
