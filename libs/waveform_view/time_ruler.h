/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace bitoscope::waveform_view {

class Viewport;

using LabelWidthFn = std::function<double(const std::string&)>;

struct RulerTick {
  double pixelX = 0.0;
  int64_t sample = 0;  // WHY: Draw tick lines from sample via overlayColumnX(), not pixelX: pixelX is only for label layout.
  bool major = false;
  std::string label;
  double labelPixelX = 0.0;
};

std::vector<RulerTick> buildTimeRuler(const Viewport& viewport, double minMajorSpacingPixels = 80.0,
                                       int minorTicksPerMajor = 5,
                                       const LabelWidthFn& measureLabelWidth = {});

}
