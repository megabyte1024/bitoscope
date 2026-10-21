/*
 * SPDX-License-Identifier: MIT
 */

#include "lanes_on_screen.h"

#include <array>

namespace bitoscope::waveform_view {

PlacedLanes placeLanes(const LaneDisplay& display, const LaneRegions3& regions, float viewTop, float scroll) {
  constexpr std::array<LanePlacement, 3> kPlacements{LanePlacement::Top, LanePlacement::Scroll,
                                                     LanePlacement::Bottom};
  PlacedLanes out;
  for (const LanePlacement placement : kPlacements) {
    const double top = placement == LanePlacement::Top      ? regions.topTop
                       : placement == LanePlacement::Bottom ? regions.bottomTop
                                                            : regions.scrollTop;
    const double height = placement == LanePlacement::Top      ? regions.topHeight
                          : placement == LanePlacement::Bottom ? regions.bottomHeight
                                                               : regions.scrollHeight;
    const float bandTop = viewTop + static_cast<float>(top);
    const float bandBottom = viewTop + static_cast<float>(top + height);
    if (bandBottom <= bandTop) continue;
    const float firstTop = placement == LanePlacement::Scroll ? bandTop - scroll : bandTop;
    for (const DisplayedLane& lane : display.lanesOf(placement)) {
      const PlacedLane placed{lane, placement, firstTop + static_cast<float>(lane.offset), bandTop, bandBottom};
      out.placed.push_back(placed);
      if (placed.visibleBottom() <= placed.visibleTop()) continue;
      out.onScreen.push_back(placed);
    }
  }
  return out;
}

std::vector<std::uint32_t> channelRowsToRender(std::span<const PlacedLane> onScreen, const LaneDisplay& allLanes,
                                               std::optional<LaneKey> dragged) {
  std::vector<std::uint32_t> rows;
  for (const PlacedLane& placed : onScreen) {
    if (placed.lane.key.kind == LaneKind::Channel) rows.push_back(placed.lane.source);
  }
  if (dragged && dragged->kind == LaneKind::Channel) {
    for (const LanePlacement placement : {LanePlacement::Top, LanePlacement::Scroll, LanePlacement::Bottom}) {
      for (const DisplayedLane& lane : allLanes.lanesOf(placement)) {
        if (lane.key == *dragged) rows.push_back(lane.source);
      }
    }
  }
  std::sort(rows.begin(), rows.end());
  rows.erase(std::unique(rows.begin(), rows.end()), rows.end());
  return rows;
}

}
