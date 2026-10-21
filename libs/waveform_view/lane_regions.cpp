/*
 * SPDX-License-Identifier: MIT
 */

#include "lane_regions.h"

#include <algorithm>

namespace bitoscope::waveform_view {

namespace {

double wantedStackHeight(double stackHeight, bool dragging, double emptyStackDropHeight) {
  stackHeight = (std::max)(0.0, stackHeight);
  if (stackHeight == 0.0 && dragging) {
    return (std::max)(0.0, emptyStackDropHeight);
  }
  return stackHeight;
}

struct Region {
  LanePlacement placement;
  double top;
  double height;
  std::span<const double> laneHeights;
};

std::size_t gapAt(double rel, std::span<const double> laneHeights) {
  double top = 0.0;
  std::size_t index = 0;
  for (const double h : laneHeights) {
    if (top + h * 0.5 > rel) {
      break;
    }
    top += h;
    ++index;
  }
  return index;
}

double heightBefore(std::span<const double> laneHeights, std::size_t count) {
  double sum = 0.0;
  for (std::size_t i = 0; i < count && i < laneHeights.size(); ++i) {
    sum += laneHeights[i];
  }
  return sum;
}

}

LaneRegions3 laneRegions3(double viewHeight, double scrollingContentHeight, double topStackHeight,
                          double bottomStackHeight, double minScrollAreaHeight, bool dragging,
                          double emptyStackDropHeight) {
  viewHeight = (std::max)(0.0, viewHeight);
  scrollingContentHeight = (std::max)(0.0, scrollingContentHeight);
  LaneRegions3 r;
  double room = (std::max)(0.0, viewHeight - minScrollAreaHeight);
  r.topTop = 0.0;
  r.topHeight = (std::min)(wantedStackHeight(topStackHeight, dragging, emptyStackDropHeight), room);
  room -= r.topHeight;
  r.bottomHeight = (std::min)(wantedStackHeight(bottomStackHeight, dragging, emptyStackDropHeight), room);
  r.scrollTop = r.topHeight;
  r.scrollHeight = viewHeight - r.topHeight - r.bottomHeight;
  r.bottomTop = r.scrollTop + r.scrollHeight;
  r.contentHeight = scrollingContentHeight + r.topHeight + r.bottomHeight;
  r.maxScroll = (std::max)(0.0, scrollingContentHeight - r.scrollHeight);
  return r;
}

bool inTopStack(double y, const LaneRegions3& r) {
  return r.topHeight > 0.0 && y >= r.topTop && y < r.topTop + r.topHeight;
}

bool inScrollArea(double y, const LaneRegions3& r) {
  return r.scrollHeight > 0.0 && y >= r.scrollTop && y < r.scrollTop + r.scrollHeight;
}

bool inBottomStack(double y, const LaneRegions3& r) {
  return r.bottomHeight > 0.0 && y >= r.bottomTop && y < r.bottomTop + r.bottomHeight;
}

DropTarget dropTarget(double y, const LaneRegions3& r, std::span<const double> topLaneHeights,
                      std::span<const double> scrollLaneHeights, std::span<const double> bottomLaneHeights,
                      double scrollOffset) {
  const Region regions[] = {
      {LanePlacement::Top, r.topTop, r.topHeight, topLaneHeights},
      {LanePlacement::Scroll, r.scrollTop, r.scrollHeight, scrollLaneHeights},
      {LanePlacement::Bottom, r.bottomTop, r.bottomHeight, bottomLaneHeights},
  };

  const Region* best = nullptr;
  for (const Region& region : regions) {
    if (region.height <= 0.0) {
      continue;
    }
    const bool inside = y >= region.top && y < region.top + region.height;
    if (inside) {
      best = &region;
      break;
    }
    if (y < 0.0) {
      best = &region;
      break;
    }
    best = &region;
  }
  if (best == nullptr) {
    return DropTarget{LanePlacement::Scroll, 0, 0.0};
  }

  const double clampedY = std::clamp(y, best->top, best->top + best->height);
  const double shift = (best->placement == LanePlacement::Scroll) ? scrollOffset : 0.0;
  const double rel = clampedY - best->top + shift;

  const std::size_t index = gapAt(rel, best->laneHeights);
  const double lineY = best->top - shift + heightBefore(best->laneHeights, index);
  return DropTarget{best->placement, index, std::clamp(lineY, best->top, best->top + best->height)};
}

std::size_t indexAfterRemoval(std::size_t dropIndex, std::size_t draggedIndexInSamePlacement) {
  if (draggedIndexInSamePlacement == kNoDraggedLane || dropIndex <= draggedIndexInSamePlacement) {
    return dropIndex;
  }
  return dropIndex - 1;
}

double autoScrollDelta(double y, const LaneRegions3& r, double edgeBand, double maxStep) {
  if (edgeBand <= 0.0 || maxStep <= 0.0 || r.scrollHeight <= 0.0) {
    return 0.0;
  }
  const double band = (std::min)(edgeBand, r.scrollHeight * 0.5);
  const double areaTop = r.scrollTop;
  const double areaBottom = r.scrollTop + r.scrollHeight;
  const double viewEnd = r.bottomTop + r.bottomHeight;

  const bool aboveView = y < r.topTop && r.topHeight <= 0.0;
  if (aboveView || (y >= areaTop && y < areaTop + band)) {
    const double depth = (std::min)(band, areaTop + band - y);
    return -maxStep * depth / band;
  }
  const bool belowView = y >= viewEnd && r.bottomHeight <= 0.0;
  if (belowView || (y > areaBottom - band && y < areaBottom)) {
    const double depth = (std::min)(band, y - (areaBottom - band));
    return maxStep * depth / band;
  }
  return 0.0;
}

double dragGhostTop(double y, double grabOffset, double laneHeight, double viewHeight) {
  return std::clamp(y - grabOffset, 0.0, (std::max)(0.0, viewHeight - laneHeight));
}

LaneDragStep laneDragStep(bool leftMouseDown, bool escapePressed) {
  if (escapePressed) {
    return LaneDragStep::Cancel;
  }
  return leftMouseDown ? LaneDragStep::Continue : LaneDragStep::Drop;
}

}
