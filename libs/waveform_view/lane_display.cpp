/*
 * SPDX-License-Identifier: MIT
 */

#include "lane_display.h"

#include <algorithm>

#include "lane_regions.h"

namespace bitoscope::waveform_view {

const std::vector<DisplayedLane>& LaneDisplay::lanesOf(LanePlacement placement) const {
  switch (placement) {
    case LanePlacement::Top: return top;
    case LanePlacement::Bottom: return bottom;
    case LanePlacement::Scroll: break;
  }
  return scroll;
}

namespace {

std::optional<std::uint32_t> shownLaneOf(std::uint32_t device, std::span<const std::uint32_t> laneDevice,
                                         std::span<const std::uint32_t> shownLanes) {
  for (const std::uint32_t lane : shownLanes) {
    if (lane < laneDevice.size() && laneDevice[lane] == device) return lane;
  }
  return std::nullopt;
}

LaneActionResult unchanged() { return {}; }

}

LaneDisplay laneDisplay(const LaneOrder& order, std::span<const std::uint32_t> laneDevice,
                        std::span<const std::uint32_t> shownLanes, std::span<const double> decoderHeights,
                        double channelHeight, double scrollPad) {
  LaneDisplay display;
  double topY = 0.0;
  double scrollY = scrollPad;
  double bottomY = 0.0;
  for (const LaneEntry& entry : order.entries()) {
    DisplayedLane lane;
    lane.key = entry.key;
    if (entry.key.kind == LaneKind::Channel) {
      const auto shown = shownLaneOf(entry.key.index, laneDevice, shownLanes);
      if (!shown) continue;
      lane.source = *shown;
      lane.height = channelHeight;
    } else {
      if (entry.key.index >= decoderHeights.size()) continue;
      lane.source = entry.key.index;
      lane.height = decoderHeights[entry.key.index];
    }
    double& y = entry.placement == LanePlacement::Top ? topY : entry.placement == LanePlacement::Bottom ? bottomY : scrollY;
    lane.offset = y;
    y += lane.height;
    switch (entry.placement) {
      case LanePlacement::Top: display.top.push_back(lane); break;
      case LanePlacement::Scroll: display.scroll.push_back(lane); break;
      case LanePlacement::Bottom: display.bottom.push_back(lane); break;
    }
  }
  display.topHeight = topY;
  display.scrollContentHeight = scrollY + scrollPad;
  display.bottomHeight = bottomY;
  return display;
}

namespace {

double closeUp(std::vector<DisplayedLane>& lanes, LaneKey key, double start) {
  std::erase_if(lanes, [&](const DisplayedLane& lane) { return lane.key == key; });
  double y = start;
  for (DisplayedLane& lane : lanes) {
    lane.offset = y;
    y += lane.height;
  }
  return y;
}

}

LaneDisplay withoutLane(const LaneDisplay& display, LaneKey key) {
  LaneDisplay cut = display;
  cut.topHeight = closeUp(cut.top, key, 0.0);
  cut.bottomHeight = closeUp(cut.bottom, key, 0.0);
  const double scrollPad = display.scroll.empty() ? display.scrollContentHeight * 0.5 : display.scroll.front().offset;
  cut.scrollContentHeight = closeUp(cut.scroll, key, scrollPad) + scrollPad;
  return cut;
}

std::optional<DraggedLanePlace> draggedLanePlace(const LaneDisplay& display, LaneKey dragged, double pointerY,
                                                 double grabOffset, double viewHeight) {
  for (const LanePlacement placement : {LanePlacement::Top, LanePlacement::Scroll, LanePlacement::Bottom}) {
    for (const DisplayedLane& lane : display.lanesOf(placement)) {
      if (lane.key == dragged) {
        return DraggedLanePlace{dragGhostTop(pointerY, grabOffset, lane.height, viewHeight), lane.height};
      }
    }
  }
  return std::nullopt;
}

std::optional<LaneHit> laneUnder(double y, const LaneRegions3& regions, const LaneDisplay& display,
                                 double scrollOffset) {
  LanePlacement placement;
  double origin;
  if (inTopStack(y, regions)) {
    placement = LanePlacement::Top;
    origin = regions.topTop;
  } else if (inScrollArea(y, regions)) {
    placement = LanePlacement::Scroll;
    origin = regions.scrollTop - scrollOffset;
  } else if (inBottomStack(y, regions)) {
    placement = LanePlacement::Bottom;
    origin = regions.bottomTop;
  } else {
    return std::nullopt;
  }
  const std::vector<DisplayedLane>& lanes = display.lanesOf(placement);
  for (std::size_t i = 0; i < lanes.size(); ++i) {
    const double top = origin + lanes[i].offset;
    if (y >= top && y < top + lanes[i].height) return LaneHit{placement, i, top, lanes[i].height};
  }
  return std::nullopt;
}

std::vector<double> dropHeights(const LaneDisplay& display, LanePlacement placement, double scrollPad) {
  std::vector<double> heights;
  for (const DisplayedLane& lane : display.lanesOf(placement)) heights.push_back(lane.height);
  if (placement == LanePlacement::Scroll && !heights.empty()) {
    heights.front() += scrollPad;
    heights.back() += scrollPad;
  }
  return heights;
}

LaneOrder defaultLaneOrder(std::span<const std::uint32_t> deviceChannels, std::span<const std::size_t> decoderOrder) {
  LaneOrder channelsOnly = LaneOrder::fresh(deviceChannels, 0);
  std::vector<LaneEntry> entries = channelsOnly.entries();
  for (const std::size_t d : decoderOrder) {
    entries.push_back({{LaneKind::Decoder, static_cast<std::uint32_t>(d)}, LanePlacement::Scroll});
  }
  return LaneOrder::fromEntries(std::move(entries));
}

LaneActionResult clickPin(LaneOrder& order, LaneKey key, PinSide rememberedSide) {
  const auto placement = order.placementOfKey(key);
  if (!placement) return unchanged();
  if (pinSideOf(*placement)) return unpinLane(order, key);
  return pinLaneTo(order, key, rememberedSide);
}

LaneActionResult pinLaneTo(LaneOrder& order, LaneKey key, PinSide side) {
  const auto placement = order.placementOfKey(key);
  if (!placement) return unchanged();
  if (*placement == placementOf(side)) return {false, side};
  order.pin(key, side);
  return {true, side};
}

LaneActionResult unpinLane(LaneOrder& order, LaneKey key) {
  const LaneOrder before = order;
  if (!order.unpin(key)) return unchanged();
  return {order != before, std::nullopt};
}

LaneActionResult dropLane(LaneOrder& order, LaneKey dragged, LanePlacement placement, std::size_t gap,
                          std::span<const DisplayedLane> shown) {
  if (!order.placementOfKey(dragged)) return unchanged();
  const std::vector<LaneKey> keys = order.keysOf(placement);
  const auto positionOf = [&](LaneKey key) -> std::optional<std::size_t> {
    const auto it = std::find(keys.begin(), keys.end(), key);
    if (it == keys.end()) return std::nullopt;
    return static_cast<std::size_t>(it - keys.begin());
  };
  std::size_t insertAt = keys.size();
  if (gap < shown.size()) {
    insertAt = positionOf(shown[gap].key).value_or(keys.size());
  } else if (!shown.empty()) {
    if (const auto last = positionOf(shown.back().key)) insertAt = *last + 1;
  }
  const std::size_t index = indexAfterRemoval(insertAt, positionOf(dragged).value_or(kNoDraggedLane));

  const LaneOrder before = order;
  order.moveTo(dragged, placement, index);
  return {order != before, pinSideOf(placement)};
}

}
