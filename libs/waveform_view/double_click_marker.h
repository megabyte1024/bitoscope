/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <optional>

namespace bitoscope::waveform_view {

class Viewport;

enum class PointerRegion : std::uint8_t { Ruler, Traces, HeaderColumn, Splitter, RowStrip, Other };

struct DoubleClickPress {
  PointerRegion region = PointerRegion::Other;
  double pixelX = 0.0;
  int clickCount = 0;
  bool pressDragged = false;
  bool previousPressDragged = false;
  bool onLine = false;
};

std::optional<std::uint64_t> markerForDoubleClick(const DoubleClickPress& press, const Viewport& viewport,
                                                  std::uint64_t totalSamples);

}
