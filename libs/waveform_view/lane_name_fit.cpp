/*
 * SPDX-License-Identifier: MIT
 */

#include "lane_name_fit.h"

#include <algorithm>

namespace bitoscope::waveform_view {

LaneNameFit fitLaneNameWidth(const LaneNameFitInput& input) {
  const double others = input.activityBar + input.properties + input.gutter;
  LaneNameFit fit;
  fit.widest = input.available - others - input.tracesMinimum;
  fit.laneNameWidth = (std::max)(input.laneMinimum, (std::min)(input.wanted, fit.widest));
  fit.tracesWidth = (std::max)(0.0, input.available - others - fit.laneNameWidth);
  return fit;
}

}
