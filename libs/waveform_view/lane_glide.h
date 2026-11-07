/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <optional>

namespace bitoscope::waveform_view {

inline constexpr double kLaneGlideTimeConstantMs = 25.0;

inline constexpr double kLaneGlideSnapPixels = 0.5;

inline constexpr double kLaneGlideFirstFrameMs = 1000.0 / 60.0;

inline constexpr double kLaneGlideStaleMs = 100.0;

class LaneGlide {
 public:
  void addLanes(double shownScroll, double lanes, double pitch, double maxScroll, double nowMs);
  std::optional<double> step(double shownScroll, double maxScroll, double nowMs);
  void stop();
  bool active() const;
  bool wantsFrames(double nowMs) const;

 private:
  std::optional<double> target_;
  double position_ = 0.0;
  double lastMs_ = 0.0;
};

}
