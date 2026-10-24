/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace bitoscope::waveform_view {

float lanePitch(float minPitch, float headerContentHeight);

struct WaveBand {
  float top = 0.0f;
  float bottom = 0.0f;
};

WaveBand waveBand(float pitch, float waveHeight);

std::optional<std::uint32_t> waveHitAtY(double y, double firstLaneTop, double pitch, double waveTop,
                                        double waveBottom, std::uint32_t lanes);

double decoderLaneHeight(std::size_t shownRows, double rowPitch);

std::optional<std::size_t> decoderRowAtY(double y, double laneTop, double rowPitch, std::size_t shownRows);

double stackedLaneTop(double stackTop, std::span<const double> laneHeights, std::size_t index);

double stackedLanesHeight(std::span<const double> laneHeights);

std::optional<std::size_t> laneAtYByHeights(double y, double firstLaneTop, std::span<const double> laneHeights);

}
