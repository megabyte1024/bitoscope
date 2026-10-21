/*
 * SPDX-License-Identifier: MIT
 */

#include "header_press.h"

#include <algorithm>

namespace bitoscope::waveform_view {

PressTracker::PressTracker(std::int32_t threshold) {
  const std::int64_t t = (std::max)(std::int32_t{1}, threshold);
  thresholdSquared_ = t * t;
}

void PressTracker::press(PressPoint at) {
  pressAt_ = at;
  state_ = PressState::Pending;
}

PressState PressTracker::move(PressPoint at) {
  if (state_ == PressState::Pending) {
    const std::int64_t dx = std::int64_t{at.x} - pressAt_.x;
    const std::int64_t dy = std::int64_t{at.y} - pressAt_.y;
    if (dx * dx + dy * dy >= thresholdSquared_) {
      state_ = PressState::Drag;
    }
  }
  return state_;
}

PressEnd PressTracker::release(PressPoint at) {
  const PressState last = move(at);
  state_ = PressState::Idle;
  switch (last) {
    case PressState::Pending:
      return PressEnd::Click;
    case PressState::Drag:
      return PressEnd::EndDrag;
    case PressState::Idle:
      break;
  }
  return PressEnd::None;
}

void PressTracker::cancel() { state_ = PressState::Idle; }

PressState PressTracker::state() const { return state_; }

}
