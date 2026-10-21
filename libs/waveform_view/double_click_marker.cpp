/*
 * SPDX-License-Identifier: MIT
 */

#include "double_click_marker.h"

#include "interaction.h"

namespace bitoscope::waveform_view {

namespace {

constexpr int kDoubleClick = 2;

bool addsMarker(PointerRegion region) { return region == PointerRegion::Ruler || region == PointerRegion::Traces; }

}

std::optional<std::uint64_t> markerForDoubleClick(const DoubleClickPress& press, const Viewport& viewport,
                                                  std::uint64_t totalSamples) {
  if (press.clickCount != kDoubleClick || !addsMarker(press.region)) {
    return std::nullopt;
  }
  if (press.pressDragged || press.previousPressDragged || press.onLine) {
    return std::nullopt;
  }
  if (!canPlaceMarkersAndCursors(totalSamples)) {
    return std::nullopt;
  }
  return cursorSampleAtPixel(viewport, press.pixelX, totalSamples);
}

}
