/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "cursors.h"
#include "flag.h"
#include "marker_list.h"

namespace bitoscope::waveform_view {

class Viewport;

inline constexpr double kZoomStep = 1.5;

inline constexpr double kKeyPanFraction = 0.1;

inline constexpr double kWheelPanPixels = 60.0;

enum class ViewKey : uint8_t {
  Other,
  Equal,
  Minus,
  KeypadAdd,
  KeypadSubtract,
  Keypad0,
  Digit0,
  F,
  C,
  LeftArrow,
  RightArrow,
};

struct KeyModifiers {
  bool ctrl = false;
  bool shift = false;
  bool alt = false;
  bool super = false;
};

enum class ViewCommand : uint8_t {
  None,
  ZoomIn,
  ZoomOut,
  ZoomToFit,
  Zoom1to1,
  PanLeft,
  PanRight,
  ToggleCursors,
};

ViewCommand commandForKey(ViewKey key, KeyModifiers modifiers);

void applyCommand(Viewport& viewport, ViewCommand command, double anchorPixelX, uint64_t totalSamples);

struct WheelAction {
  double zoomFactor = 1.0;
  double panPixels = 0.0;
};

WheelAction wheelAction(double verticalNotches, double horizontalNotches, KeyModifiers modifiers);

void applyWheel(Viewport& viewport, const WheelAction& action, double mouseX, uint64_t totalSamples);

void applyDrag(Viewport& viewport, double mouseDeltaX, uint64_t totalSamples);

double dragScrollY(double scrollAtPress, double pressY, double pointerY, double maxScroll);

double laneScrollInRange(double scrollY, double contentHeight, double viewHeight);

struct ScrollRange {
  int64_t contents = 0;
  int64_t visible = 0;
  int64_t position = 0;
};
ScrollRange scrollRangeFor(const Viewport& viewport, uint64_t totalSamples);

void scrollTo(Viewport& viewport, int64_t position, uint64_t totalSamples);

void panToShow(Viewport& viewport, uint64_t begin, uint64_t end, uint64_t totalSamples);

double scrollToShowRow(double scrollY, double viewHeight, double rowTop, double rowHeight);

uint64_t cursorSampleAtPixel(const Viewport& viewport, double pixelX, uint64_t totalSamples);

bool canPlaceMarkersAndCursors(uint64_t totalSamples);

CursorPair defaultCursors(const Viewport& viewport, uint64_t totalSamples);

std::optional<int> cursorUnderPixel(const CursorPair& cursors, const Viewport& viewport, double pixelX,
                                    double grabPixels);

std::optional<uint32_t> laneAtY(double y, double firstLaneTop, double pitch, uint32_t lanes);

std::optional<uint64_t> sampleUnderPixel(const Viewport& viewport, double pixelX, uint64_t totalSamples);

std::optional<MarkerId> markerUnderPixel(const MarkerList& markers, const Viewport& viewport, double pixelX,
                                         double grabPixels);

enum class GrabKind : uint8_t { None, Marker, CursorFirst, CursorSecond, CursorPair };

struct GrabTarget {
  GrabKind kind = GrabKind::None;
  MarkerId markerId = 0;
};

GrabTarget grabAtPixel(const MarkerList& markers, const CursorPair& cursors, bool cursorsShown,
                      const Viewport& viewport, double pixelX, double grabPixels);

struct RulerFlagCandidate {
  GrabKind kind = GrabKind::None;
  MarkerId markerId = 0;
  double lineX = 0.0;
  FlagRect rect;
};

GrabTarget rulerPressTarget(const std::vector<RulerFlagCandidate>& candidates, double pixelX, double pixelY,
                            double lineTop, double lineBottom, double grabPixels, GrabTarget lineFallback,
                            const FlagRect* labelRect = nullptr);

int64_t pairDragDeltaSamples(const Viewport& viewport, double pressPixelX, double pixelX);

CursorPair movePairBySampleDelta(const CursorPair& original, int64_t deltaSamples, uint64_t totalSamples);

}
