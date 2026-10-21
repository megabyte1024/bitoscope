/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "lane_order.h"
#include "lane_regions.h"

namespace bitoscope::waveform_view {

struct DisplayedLane {
  LaneKey key;
  std::uint32_t source = 0;
  double offset = 0.0;
  double height = 0.0;
  bool operator==(const DisplayedLane&) const = default;
};

struct LaneDisplay {
  std::vector<DisplayedLane> top;
  std::vector<DisplayedLane> scroll;
  std::vector<DisplayedLane> bottom;
  double topHeight = 0.0;
  double scrollContentHeight = 0.0;
  double bottomHeight = 0.0;

  const std::vector<DisplayedLane>& lanesOf(LanePlacement placement) const;
};

LaneDisplay laneDisplay(const LaneOrder& order, std::span<const std::uint32_t> laneDevice,
                        std::span<const std::uint32_t> shownLanes, std::span<const double> decoderHeights,
                        double channelHeight, double scrollPad);

LaneDisplay withoutLane(const LaneDisplay& display, LaneKey key);

std::vector<double> dropHeights(const LaneDisplay& display, LanePlacement placement, double scrollPad);

struct LaneHit {
  LanePlacement placement = LanePlacement::Scroll;
  std::size_t index = 0;
  double top = 0.0;
  double height = 0.0;
  bool operator==(const LaneHit&) const = default;
};

std::optional<LaneHit> laneUnder(double y, const LaneRegions3& regions, const LaneDisplay& display,
                                 double scrollOffset);

struct DraggedLanePlace {
  double top = 0.0;
  double height = 0.0;
  bool operator==(const DraggedLanePlace&) const = default;
};

std::optional<DraggedLanePlace> draggedLanePlace(const LaneDisplay& display, LaneKey dragged, double pointerY,
                                                 double grabOffset, double viewHeight);

LaneOrder defaultLaneOrder(std::span<const std::uint32_t> deviceChannels, std::span<const std::size_t> decoderOrder);

struct LaneActionResult {
  bool changed = false;
  std::optional<PinSide> record;
  bool operator==(const LaneActionResult&) const = default;
};

LaneActionResult clickPin(LaneOrder& order, LaneKey key, PinSide rememberedSide);

LaneActionResult pinLaneTo(LaneOrder& order, LaneKey key, PinSide side);

LaneActionResult unpinLane(LaneOrder& order, LaneKey key);

LaneActionResult dropLane(LaneOrder& order, LaneKey dragged, LanePlacement placement, std::size_t gap,
                          std::span<const DisplayedLane> shown);

}
