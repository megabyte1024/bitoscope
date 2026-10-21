/*
 * SPDX-License-Identifier: MIT
 */

#include "strip_cut.h"

#include <algorithm>

namespace bitoscope::waveform_view {

std::vector<YInterval> cutStrips(YInterval line, std::span<const YInterval> strips) {
  std::vector<YInterval> pieces;
  if (line.bottom <= line.top) {
    return pieces;
  }
  std::vector<YInterval> sorted;
  sorted.reserve(strips.size());
  for (const YInterval& strip : strips) {
    if (strip.top < strip.bottom) {
      sorted.push_back(strip);
    }
  }
  std::sort(sorted.begin(), sorted.end(), [](const YInterval& a, const YInterval& b) { return a.top < b.top; });

  double from = line.top;
  for (const YInterval& strip : sorted) {
    if (strip.top > from) {
      pieces.push_back({from, (std::min)(strip.top, line.bottom)});
    }
    from = (std::max)(from, strip.bottom);
    if (from >= line.bottom) {
      return pieces;
    }
  }
  pieces.push_back({from, line.bottom});
  return pieces;
}

}
