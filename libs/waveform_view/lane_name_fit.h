/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

namespace bitoscope::waveform_view {

struct LaneNameFitInput {
  double available = 0.0;
  double activityBar = 0.0;
  double properties = 0.0;
  double gutter = 0.0;
  double wanted = 0.0;
  double laneMinimum = 0.0;
  double tracesMinimum = 0.0;
};

struct LaneNameFit {
  double widest = 0.0;
  double laneNameWidth = 0.0;
  double tracesWidth = 0.0;
};

LaneNameFit fitLaneNameWidth(const LaneNameFitInput& input);

}
