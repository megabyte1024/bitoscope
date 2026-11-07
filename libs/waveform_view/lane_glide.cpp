/*
 * SPDX-License-Identifier: MIT
 */

#include "lane_glide.h"

#include <algorithm>
#include <cmath>

#include "interaction.h"

namespace bitoscope::waveform_view {

namespace {

constexpr double kShownScrollTolerancePixels = 1.0;

}

void LaneGlide::addLanes(double shownScroll, double lanes, double pitch, double maxScroll, double nowMs) {
  const bool continuing = target_.has_value() && std::abs(shownScroll - position_) <= kShownScrollTolerancePixels;
  const double base = continuing ? *target_ : shownScroll;
  if (!continuing) {
    position_ = shownScroll;
    lastMs_ = nowMs - kLaneGlideFirstFrameMs;
  }
  const double target = laneScrollAfterWheel(base, lanes, pitch, maxScroll);
  if (target == position_) {
    target_.reset();
    return;
  }
  target_ = target;
}

std::optional<double> LaneGlide::step(double shownScroll, double maxScroll, double nowMs) {
  if (!target_) return std::nullopt;
  if (std::abs(shownScroll - position_) > kShownScrollTolerancePixels) {
    target_.reset();
    return std::nullopt;
  }
  const double target = (std::clamp)(*target_, 0.0, (std::max)(0.0, maxScroll));
  const double elapsedMs = (std::max)(0.0, nowMs - lastMs_);
  lastMs_ = nowMs;
  const double remaining = (target - position_) * std::exp(-elapsedMs / kLaneGlideTimeConstantMs);
  if (std::abs(remaining) <= kLaneGlideSnapPixels) {
    position_ = target;
    target_.reset();
  } else {
    position_ = target - remaining;
  }
  return position_;
}

void LaneGlide::stop() { target_.reset(); }

bool LaneGlide::active() const { return target_.has_value(); }

bool LaneGlide::wantsFrames(double nowMs) const { return target_.has_value() && nowMs - lastMs_ < kLaneGlideStaleMs; }

}
