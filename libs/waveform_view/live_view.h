/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <optional>

#include "viewport.h"

namespace bitoscope::waveform_view {

uint64_t liveViewTotal(uint64_t arrivedSamples, uint64_t requestedSamples);

bool followLiveEdge(Viewport& viewport, uint64_t previousTotal, uint64_t total);

struct CarriedZoom {
  double samplesPerPixel = 1.0;
  double sampleZeroPixel = 0.0;
};

CarriedZoom carriedZoomOf(const Viewport& viewport);

void startLiveView(Viewport& viewport, const std::optional<CarriedZoom>& carried, uint64_t requestedSamples);

inline constexpr uint64_t kIdleViewSamples = 1000;
uint64_t idleViewTotal(uint64_t requestedSamples);

void fitIdleView(Viewport& viewport, uint64_t requestedSamples);

}
