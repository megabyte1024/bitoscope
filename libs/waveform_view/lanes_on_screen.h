/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "lane_display.h"
#include "lane_order.h"
#include "lane_regions.h"

namespace bitoscope::waveform_view {

struct PlacedLane {
  DisplayedLane lane;
  LanePlacement placement = LanePlacement::Scroll;
  float top = 0.0f;
  float clipTop = 0.0f;
  float clipBottom = 0.0f;
  float bottom() const { return top + static_cast<float>(lane.height); }
  float visibleTop() const { return (std::max)(top, clipTop); }
  float visibleBottom() const { return (std::min)(bottom(), clipBottom); }
};

struct PlacedLanes {
  std::vector<PlacedLane> placed;
  std::vector<PlacedLane> onScreen;
};

PlacedLanes placeLanes(const LaneDisplay& display, const LaneRegions3& regions, float viewTop, float scroll);

std::vector<std::uint32_t> channelRowsToRender(std::span<const PlacedLane> onScreen, const LaneDisplay& allLanes,
                                               std::optional<LaneKey> dragged);

}
