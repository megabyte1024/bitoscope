/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <span>
#include <vector>

namespace bitoscope::waveform_view {

struct YInterval {
  double top = 0.0;
  double bottom = 0.0;
  bool operator==(const YInterval&) const = default;
};

std::vector<YInterval> cutStrips(YInterval line, std::span<const YInterval> strips);

}
