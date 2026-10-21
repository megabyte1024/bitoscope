/*
 * SPDX-License-Identifier: MIT
 */

#include "bracket.h"

#include <algorithm>

namespace bitoscope::waveform_view {

namespace {

bool drawn(const BracketLane& lane) { return lane.shown; }

double anchorY(const BracketLane& lane) { return lane.top + lane.anchor; }

bool rowVisible(const BracketLane& lane, const BracketRect& clip) {
  const double y = anchorY(lane);
  return y >= (std::max)(lane.clipTop, clip.top) && y < (std::min)(lane.clipBottom, clip.bottom);
}

std::optional<BracketSegment> across(BracketSegmentKind kind, double slotX, double y, const BracketGutter& gutter,
                                     const BracketRect& clip) {
  const double x0 = (std::max)(slotX, clip.left);
  const double x1 = (std::min)(gutter.right, clip.right);
  if (x1 <= x0) return std::nullopt;
  return BracketSegment{kind, x0, y, x1, y};
}

}

BracketLane draggedBracketLane(double top, double height, double anchor, const BracketRect& area) {
  return BracketLane{true, top, height, anchor, area.top, area.bottom};
}

double bracketSlotX(const BracketGutter& gutter, std::size_t slot) {
  return gutter.right - (static_cast<double>(slot) + 0.5) * gutter.slotStep;
}

std::vector<std::optional<std::size_t>> bracketSlots(const std::vector<bool>& hasBracket) {
  std::vector<std::optional<std::size_t>> slots(hasBracket.size());
  std::size_t next = 0;
  for (std::size_t i = 0; i < hasBracket.size(); ++i) {
    if (hasBracket[i]) slots[i] = next++;
  }
  return slots;
}

double bracketGutterWidth(std::size_t slots, double slotStep) { return static_cast<double>(slots) * slotStep; }

std::vector<BracketSegment> bracketSegments(const BracketLane& decoder, std::span<const BracketLane> reads,
                                            std::size_t slot, const BracketGutter& gutter, const BracketRect& clip) {
  if (!drawn(decoder)) return {};
  double first = anchorY(decoder);
  double last = first;
  bool anyRead = false;
  for (const BracketLane& read : reads) {
    if (!drawn(read)) continue;
    anyRead = true;
    first = (std::min)(first, anchorY(read));
    last = (std::max)(last, anchorY(read));
  }
  if (!anyRead) return {};

  const double x = bracketSlotX(gutter, slot);
  std::vector<BracketSegment> segments;
  const double spanTop = (std::max)(first, clip.top);
  const double spanBottom = (std::min)(last, clip.bottom);
  if (x >= clip.left && x <= clip.right && spanBottom > spanTop) {
    segments.push_back(BracketSegment{BracketSegmentKind::Span, x, spanTop, x, spanBottom});
  }
  for (const BracketLane& read : reads) {
    if (!drawn(read) || !rowVisible(read, clip)) continue;
    if (const auto tick = across(BracketSegmentKind::Tick, x, anchorY(read), gutter, clip)) segments.push_back(*tick);
  }
  if (rowVisible(decoder, clip)) {
    if (const auto corner = across(BracketSegmentKind::Corner, x, anchorY(decoder), gutter, clip)) {
      segments.push_back(*corner);
    }
  }
  return segments;
}

}
