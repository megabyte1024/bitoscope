/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <vector>

namespace bitoscope::waveform_view {

struct BracketLane {
  bool shown = true;
  double top = 0.0;
  double height = 0.0;
  double anchor = 0.0;
  double clipTop = 0.0;
  double clipBottom = 0.0;
  bool operator==(const BracketLane&) const = default;
};

struct BracketGutter {
  double left = 0.0;
  double right = 0.0;
  double slotStep = 0.0;
  bool operator==(const BracketGutter&) const = default;
};

struct BracketRect {
  double left = 0.0;
  double top = 0.0;
  double right = 0.0;
  double bottom = 0.0;
  bool operator==(const BracketRect&) const = default;
};

enum class BracketSegmentKind { Span, Tick, Corner };

struct BracketSegment {
  BracketSegmentKind kind = BracketSegmentKind::Span;
  double x0 = 0.0;
  double y0 = 0.0;
  double x1 = 0.0;
  double y1 = 0.0;
  bool operator==(const BracketSegment&) const = default;
};

BracketLane draggedBracketLane(double top, double height, double anchor, const BracketRect& area);

double bracketSlotX(const BracketGutter& gutter, std::size_t slot);

std::vector<std::optional<std::size_t>> bracketSlots(const std::vector<bool>& hasBracket);

double bracketGutterWidth(std::size_t slots, double slotStep);

std::vector<BracketSegment> bracketSegments(const BracketLane& decoder, std::span<const BracketLane> reads,
                                            std::size_t slot, const BracketGutter& gutter, const BracketRect& clip);

}
