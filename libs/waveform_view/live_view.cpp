/*
 * SPDX-License-Identifier: MIT
 */

#include "live_view.h"

#include <algorithm>
#include <limits>

namespace bitoscope::waveform_view {

uint64_t liveViewTotal(uint64_t arrivedSamples, uint64_t requestedSamples) {
  return (std::max)(arrivedSamples, requestedSamples);
}

bool followLiveEdge(Viewport& viewport, uint64_t previousTotal, uint64_t total) {
  const double width = static_cast<double>(viewport.widthPixels());
  const double previousPixel = viewport.sampleToPixel(static_cast<double>(previousTotal));
  const double newestPixel = viewport.sampleToPixel(static_cast<double>(total));
  if (previousPixel < 0.0 || previousPixel > width || newestPixel <= width) return false;
  viewport.panPixels(newestPixel - width, (std::numeric_limits<uint64_t>::max)() / 4);
  return true;
}

CarriedZoom carriedZoomOf(const Viewport& viewport) {
  return CarriedZoom{viewport.samplesPerPixel(), viewport.sampleToPixel(0.0)};
}

void startLiveView(Viewport& viewport, const std::optional<CarriedZoom>& carried, uint64_t requestedSamples) {
  if (!carried.has_value()) {
    viewport.zoomToFit(requestedSamples);
    return;
  }
  const double width = static_cast<double>(viewport.widthPixels());
  const double startPixel =
    (carried->sampleZeroPixel >= 0.0 && carried->sampleZeroPixel < width) ? carried->sampleZeroPixel : 0.0;
  viewport.zoomTo(carried->samplesPerPixel, startPixel, requestedSamples);
}

uint64_t idleViewTotal(uint64_t requestedSamples) {
  return requestedSamples != 0 ? requestedSamples : kIdleViewSamples;
}

void fitIdleView(Viewport& viewport, uint64_t requestedSamples) {
  viewport.zoomToFit(idleViewTotal(requestedSamples));
}

}
