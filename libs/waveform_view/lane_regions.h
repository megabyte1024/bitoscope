/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstddef>
#include <span>

#include "lane_order.h"

namespace bitoscope::waveform_view {

struct LaneRegions3 {
  double topTop = 0.0;
  double topHeight = 0.0;
  double scrollTop = 0.0;
  double scrollHeight = 0.0;
  double bottomTop = 0.0;
  double bottomHeight = 0.0;
  double contentHeight = 0.0;
  double maxScroll = 0.0;
  bool operator==(const LaneRegions3&) const = default;
};

LaneRegions3 laneRegions3(double viewHeight, double scrollingContentHeight, double topStackHeight,
                          double bottomStackHeight, double minScrollAreaHeight, bool dragging = false,
                          double emptyStackDropHeight = 0.0);

bool inTopStack(double y, const LaneRegions3& regions);
bool inScrollArea(double y, const LaneRegions3& regions);
bool inBottomStack(double y, const LaneRegions3& regions);

struct DropTarget {
  LanePlacement placement = LanePlacement::Scroll;
  std::size_t index = 0;
  double insertionLineY = 0.0;
  bool operator==(const DropTarget&) const = default;
};

DropTarget dropTarget(double y, const LaneRegions3& regions, std::span<const double> topLaneHeights,
                      std::span<const double> scrollLaneHeights, std::span<const double> bottomLaneHeights,
                      double scrollOffset);

inline constexpr std::size_t kNoDraggedLane = static_cast<std::size_t>(-1);

std::size_t indexAfterRemoval(std::size_t dropIndex, std::size_t draggedIndexInSamePlacement);

double autoScrollDelta(double y, const LaneRegions3& regions, double edgeBand, double maxStep);

double dragGhostTop(double y, double grabOffset, double laneHeight, double viewHeight);

enum class LaneDragStep { Continue, Drop, Cancel };
LaneDragStep laneDragStep(bool leftMouseDown, bool escapePressed);

}
