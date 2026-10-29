/*
 * SPDX-License-Identifier: MIT
 */

#include "pointer_grab.h"

#include "flag.h"

namespace bitoscope::waveform_view {

void PointerGrab::begin(GrabTarget target, double pixelX, const CursorPair& cursorsAtPress) {
  kind_ = target.kind;
  markerId_ = target.kind == GrabKind::Marker ? target.markerId : 0;
  if (kind_ == GrabKind::CursorPair) {
    pairPressPixelX_ = pixelX;
    pairAtPress_ = cursorsAtPress;
  }
}

void PointerGrab::release() { kind_ = GrabKind::None; }

std::optional<MarkerId> PointerGrab::marker() const {
  if (kind_ != GrabKind::Marker) return std::nullopt;
  return markerId_;
}

DragOutcome PointerGrab::dragTo(MarkerList& markers, std::optional<CursorPair>& cursors, const Viewport& viewport,
                                double pixelX, uint64_t totalSamples, bool pressDragged) const {
  switch (kind_) {
    case GrabKind::Marker:
      if (!pressDragged) return DragOutcome::HeldStill;
      markers.moveTo(markerId_, cursorSampleAtPixel(viewport, pixelX, totalSamples));
      return DragOutcome::Moved;
    case GrabKind::CursorPair:
      cursors = movePairBySampleDelta(pairAtPress_, pairDragDeltaSamples(viewport, pairPressPixelX_, pixelX),
                                      totalSamples);
      return DragOutcome::Moved;
    case GrabKind::CursorFirst:
    case GrabKind::CursorSecond: {
      if (!cursors) return DragOutcome::NotHeld;
      const uint64_t at = cursorSampleAtPixel(viewport, pixelX, totalSamples);
      cursors = kind_ == GrabKind::CursorFirst ? CursorPair(at, cursors->second()) : CursorPair(cursors->first(), at);
      return DragOutcome::Moved;
    }
    case GrabKind::None:
      return DragOutcome::NotHeld;
  }
  return DragOutcome::NotHeld;
}

bool PointerGrab::selectsMarkerOnRelease(bool pressDragged) const {
  return kind_ == GrabKind::Marker && !pressDragged;
}

GrabTarget tracesPressTarget(GrabTarget lineTarget, bool cursorsGrabbable, bool overWidthLabel) {
  if (lineTarget.kind != GrabKind::None) return lineTarget;
  if (cursorsGrabbable && overWidthLabel) return GrabTarget{GrabKind::CursorPair, 0};
  return lineTarget;
}

PointerShape rulerPointerShape(const PointerGrab& grab, bool overMarkerFlag, bool overCursorFlag, bool overLine) {
  if (grab.active()) return PointerShape::ResizeEW;
  if (hoverCursorFor(overMarkerFlag, overCursorFlag, overLine, false) == HoverCursor::ResizeEW) {
    return PointerShape::ResizeEW;
  }
  return overMarkerFlag ? PointerShape::Arrow : PointerShape::Unchanged;
}

PointerShape tracesPointerShape(const PointerGrab& grab, bool overGrabTarget, bool panDragging) {
  if (grab.active() || overGrabTarget) return PointerShape::ResizeEW;
  return panDragging ? PointerShape::Hand : PointerShape::Unchanged;
}

}
