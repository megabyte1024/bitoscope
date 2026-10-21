/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>

namespace bitoscope::waveform_view {

struct PressPoint {
  std::int32_t x = 0;
  std::int32_t y = 0;
  bool operator==(const PressPoint&) const = default;
};

enum class PressState { Idle, Pending, Drag };

enum class PressEnd { None, Click, EndDrag };

class PressTracker {
 public:
  explicit PressTracker(std::int32_t threshold);

  void press(PressPoint at);
  PressState move(PressPoint at);
  PressEnd release(PressPoint at);
  void cancel();
  PressState state() const;

 private:
  std::int64_t thresholdSquared_ = 1;
  PressPoint pressAt_;
  PressState state_ = PressState::Idle;
};

}
