/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <optional>

#include "cursors.h"
#include "interaction.h"
#include "marker_list.h"

namespace bitoscope::waveform_view {

class Viewport;

enum class DragOutcome : uint8_t { NotHeld, HeldStill, Moved };

class PointerGrab {
 public:
  void begin(GrabTarget target, double pixelX, const CursorPair& cursorsAtPress);
  void release();

  bool active() const { return kind_ != GrabKind::None; }
  std::optional<MarkerId> marker() const;
  bool holdsCursor() const { return kind_ == GrabKind::CursorFirst || kind_ == GrabKind::CursorSecond; }

  DragOutcome dragTo(MarkerList& markers, std::optional<CursorPair>& cursors, const Viewport& viewport, double pixelX,
                     uint64_t totalSamples, bool pressDragged) const;

  bool selectsMarkerOnRelease(bool pressDragged) const;

 private:
  GrabKind kind_ = GrabKind::None;
  MarkerId markerId_ = 0;
  double pairPressPixelX_ = 0.0;
  CursorPair pairAtPress_{0, 0};
};

GrabTarget tracesPressTarget(GrabTarget lineTarget, bool cursorsGrabbable, bool overWidthLabel);

enum class PointerShape : uint8_t { Unchanged, Arrow, ResizeEW, Hand };

PointerShape rulerPointerShape(const PointerGrab& grab, bool overMarkerFlag, bool overCursorFlag, bool overLine);

PointerShape tracesPointerShape(const PointerGrab& grab, bool overGrabTarget, bool panDragging);

}
