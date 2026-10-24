/*
 * SPDX-License-Identifier: MIT
 */

#include "interaction.h"

#include <algorithm>
#include <cmath>

#include "viewport.h"

namespace bitoscope::waveform_view {

namespace {

bool reservedModifier(KeyModifiers m) { return m.ctrl || m.alt || m.super; }

}

ViewCommand commandForKey(ViewKey key, KeyModifiers modifiers) {
  if (reservedModifier(modifiers)) return ViewCommand::None;
  switch (key) {
    case ViewKey::Equal:
    case ViewKey::KeypadAdd:
      return ViewCommand::ZoomIn;
    case ViewKey::Minus:
    case ViewKey::KeypadSubtract:
      return ViewCommand::ZoomOut;
    case ViewKey::F:
      return ViewCommand::ZoomToFit;
    case ViewKey::Digit0:
      return ViewCommand::Zoom1to1;
    case ViewKey::LeftArrow:
      return ViewCommand::PanLeft;
    case ViewKey::RightArrow:
      return ViewCommand::PanRight;
    case ViewKey::C:
      return ViewCommand::ToggleCursors;
    case ViewKey::Keypad0:  // WHY: Keypad 0 maps to nothing: left to the GUI's Reset Zoom (Ctrl+NumPad0), even without Ctrl.
    case ViewKey::Other:
      break;
  }
  return ViewCommand::None;
}

void applyCommand(Viewport& viewport, ViewCommand command, double anchorPixelX, uint64_t totalSamples) {
  const double width = static_cast<double>(viewport.widthPixels());
  switch (command) {
    case ViewCommand::ZoomIn:
      viewport.zoomAroundPixel(1.0 / kZoomStep, anchorPixelX, totalSamples);
      break;
    case ViewCommand::ZoomOut:
      viewport.zoomAroundPixel(kZoomStep, anchorPixelX, totalSamples);
      break;
    case ViewCommand::ZoomToFit:
      viewport.zoomToFit(totalSamples);
      break;
    case ViewCommand::Zoom1to1:
      viewport.zoom1to1(anchorPixelX, totalSamples);
      break;
    case ViewCommand::PanLeft:
      viewport.panPixels(-kKeyPanFraction * width, totalSamples);
      break;
    case ViewCommand::PanRight:
      viewport.panPixels(kKeyPanFraction * width, totalSamples);
      break;
    case ViewCommand::ToggleCursors:
    case ViewCommand::None:
      break;
  }
}

WheelAction wheelAction(double verticalNotches, double horizontalNotches, KeyModifiers modifiers) {
  WheelAction action;
  if (reservedModifier(modifiers)) return action;
  if (modifiers.shift) {
    action.panPixels = (horizontalNotches - verticalNotches) * kWheelPanPixels;
    return action;
  }
  action.zoomFactor = std::pow(kZoomStep, -verticalNotches);
  action.panPixels = horizontalNotches * kWheelPanPixels;
  return action;
}

void applyWheel(Viewport& viewport, const WheelAction& action, double mouseX, uint64_t totalSamples) {
  if (action.zoomFactor != 1.0) viewport.zoomAroundPixel(action.zoomFactor, mouseX, totalSamples);
  if (action.panPixels != 0.0) viewport.panPixels(action.panPixels, totalSamples);
}

void applyDrag(Viewport& viewport, double mouseDeltaX, uint64_t totalSamples) {
  viewport.panPixels(-mouseDeltaX, totalSamples);
}

double dragScrollY(double scrollAtPress, double pressY, double pointerY, double maxScroll) {
  const double target = scrollAtPress - (pointerY - pressY);
  return (std::clamp)(target, 0.0, (std::max)(0.0, maxScroll));
}

ScrollRange scrollRangeFor(const Viewport& viewport, uint64_t totalSamples) {
  const double visible = viewport.samplesPerPixel() * viewport.widthPixels();
  const double minFirst = viewport.minFirstVisibleSample();
  const double maxFirst = viewport.maxFirstVisibleSample(totalSamples);
  ScrollRange range;
  range.contents = std::llround(maxFirst - minFirst + visible);
  range.visible = std::llround(visible);
  range.position = std::llround(viewport.firstVisibleSample() - minFirst);
  return range;
}

void scrollTo(Viewport& viewport, int64_t position, uint64_t totalSamples) {
  const double target = static_cast<double>(position) + viewport.minFirstVisibleSample();
  viewport.panPixels((target - viewport.firstVisibleSample()) / viewport.samplesPerPixel(), totalSamples);
}

void panToShow(Viewport& viewport, uint64_t begin, uint64_t end, uint64_t totalSamples) {
  const double visibleSamples = viewport.samplesPerPixel() * static_cast<double>(viewport.widthPixels());
  const double first = static_cast<double>(begin);
  const double length = static_cast<double>(end) - first;
  const double target = length <= visibleSamples ? first + (length - visibleSamples) / 2.0 : first;
  viewport.panPixels((target - viewport.firstVisibleSample()) / viewport.samplesPerPixel(), totalSamples);
}

double scrollToShowRow(double scrollY, double viewHeight, double rowTop, double rowHeight) {
  if (rowTop < scrollY || rowHeight > viewHeight) return rowTop;
  const double rowBottom = rowTop + rowHeight;
  if (rowBottom > scrollY + viewHeight) return rowBottom - viewHeight;
  return scrollY;
}

uint64_t cursorSampleAtPixel(const Viewport& viewport, double pixelX, uint64_t totalSamples) {
  const double sample = std::round(viewport.pixelToSample(pixelX));
  if (sample <= 0.0) return 0;
  if (sample >= static_cast<double>(totalSamples)) return totalSamples;
  return static_cast<uint64_t>(sample);
}

bool canPlaceMarkersAndCursors(uint64_t totalSamples) { return totalSamples > 0; }

CursorPair defaultCursors(const Viewport& viewport, uint64_t totalSamples) {
  const double total = static_cast<double>(totalSamples);
  const double first = (std::clamp)(viewport.firstVisibleSample(), 0.0, total);
  const double last = (std::clamp)(viewport.pixelToSample(viewport.widthPixels()), 0.0, total);
  const auto at = [&](double fraction) { return static_cast<uint64_t>(std::llround(first + (last - first) * fraction)); };
  return CursorPair(at(1.0 / 3.0), at(2.0 / 3.0));
}

std::optional<int> cursorUnderPixel(const CursorPair& cursors, const Viewport& viewport, double pixelX,
                                    double grabPixels) {
  const double d0 = std::abs(viewport.samplePixelX(static_cast<int64_t>(cursors.first())) - pixelX);
  const double d1 = std::abs(viewport.samplePixelX(static_cast<int64_t>(cursors.second())) - pixelX);
  if (d0 > grabPixels && d1 > grabPixels) return std::nullopt;
  return d1 < d0 ? 1 : 0;
}

std::optional<uint32_t> laneAtY(double y, double firstLaneTop, double pitch, uint32_t lanes) {
  if (pitch <= 0.0 || y < firstLaneTop) return std::nullopt;
  const double lane = std::floor((y - firstLaneTop) / pitch);
  if (lane >= static_cast<double>(lanes)) return std::nullopt;
  return static_cast<uint32_t>(lane);
}

std::optional<uint64_t> sampleUnderPixel(const Viewport& viewport, double pixelX, uint64_t totalSamples) {
  const double sample = std::floor(viewport.pixelToSample(pixelX));
  if (sample < 0.0 || sample >= static_cast<double>(totalSamples)) return std::nullopt;
  return static_cast<uint64_t>(sample);
}

std::optional<MarkerId> markerUnderPixel(const MarkerList& markers, const Viewport& viewport, double pixelX,
                                         double grabPixels) {
  std::optional<MarkerId> best;
  double bestDistance = 0.0;
  for (const Marker& marker : markers.markers()) {
    const double distance = std::abs(viewport.samplePixelX(static_cast<int64_t>(marker.sample)) - pixelX);
    if (distance <= grabPixels && (!best.has_value() || distance < bestDistance)) {
      best = marker.id;
      bestDistance = distance;
    }
  }
  return best;
}

GrabTarget grabAtPixel(const MarkerList& markers, const CursorPair& cursors, bool cursorsShown,
                       const Viewport& viewport, double pixelX, double grabPixels) {
  GrabTarget best;
  double bestDistance = 0.0;
  const double width = static_cast<double>(viewport.widthPixels());
  const auto onScreen = [&](double linePixel) { return linePixel >= 0.0 && linePixel <= width; };
  const auto consider = [&](GrabKind kind, double linePixel, MarkerId id) {
    if (!onScreen(linePixel)) return;
    const double distance = std::abs(linePixel - pixelX);
    if (distance > grabPixels) return;
    const bool better = best.kind == GrabKind::None || distance < bestDistance ||
                        (distance == bestDistance && kind == GrabKind::Marker && best.kind != GrabKind::Marker);
    if (better) {
      best.kind = kind;
      best.markerId = id;
      bestDistance = distance;
    }
  };
  if (cursorsShown) {
    consider(GrabKind::CursorFirst, viewport.samplePixelX(static_cast<int64_t>(cursors.first())), 0);
    consider(GrabKind::CursorSecond, viewport.samplePixelX(static_cast<int64_t>(cursors.second())), 0);
  }
  for (const Marker& marker : markers.markers()) {
    consider(GrabKind::Marker, viewport.samplePixelX(static_cast<int64_t>(marker.sample)), marker.id);
  }
  return best;
}

GrabTarget rulerPressTarget(const std::vector<RulerFlagCandidate>& candidates, double pixelX, double pixelY,
                            double lineTop, double lineBottom, double grabPixels, GrabTarget lineFallback,
                            const FlagRect* labelRect) {
  GrabTarget best;
  double bestDistance = 0.0;
  for (const RulerFlagCandidate& c : candidates) {
    if (hitTestFlag(pixelX, pixelY, c.lineX, c.rect, lineTop, lineBottom, grabPixels) != FlagZone::Flag) {
      continue;
    }
    const double distance = std::abs(c.lineX - pixelX);
    const bool better = best.kind == GrabKind::None || distance < bestDistance ||
                        (distance == bestDistance && c.kind == GrabKind::Marker && best.kind != GrabKind::Marker);
    if (better) {
      best.kind = c.kind;
      best.markerId = c.markerId;
      bestDistance = distance;
    }
  }
  if (best.kind != GrabKind::None) return best;
  if (lineFallback.kind != GrabKind::None) return lineFallback;
  if (labelRect != nullptr && flagRectContains(*labelRect, pixelX, pixelY)) {
    return GrabTarget{GrabKind::CursorPair, 0};
  }
  return lineFallback;
}

int64_t pairDragDeltaSamples(const Viewport& viewport, double pressPixelX, double pixelX) {
  return std::llround(viewport.pixelToSample(pixelX) - viewport.pixelToSample(pressPixelX));
}

CursorPair movePairBySampleDelta(const CursorPair& original, int64_t deltaSamples, uint64_t totalSamples) {
  const int64_t first = static_cast<int64_t>(original.first());
  const int64_t second = static_cast<int64_t>(original.second());
  const int64_t total = static_cast<int64_t>(totalSamples);
  const int64_t minPos = (std::min)(first, second);
  const int64_t maxPos = (std::max)(first, second);
  const int64_t clampedDelta = (std::clamp)(deltaSamples, -minPos, total - maxPos);
  return CursorPair(static_cast<uint64_t>(first + clampedDelta), static_cast<uint64_t>(second + clampedDelta));
}

double laneScrollInRange(double scrollY, double contentHeight, double viewHeight) {
  const double maxScroll = (std::max)(0.0, contentHeight - viewHeight);
  return (std::clamp)(scrollY, 0.0, maxScroll);
}

}
