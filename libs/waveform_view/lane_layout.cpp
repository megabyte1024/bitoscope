/*
 * SPDX-License-Identifier: MIT
 */

#include "lane_layout.h"

#include <algorithm>
#include <cmath>

#include "interaction.h"

namespace bitoscope::waveform_view {

float lanePitch(float minPitch, float headerContentHeight) {
  return minPitch > headerContentHeight ? minPitch : headerContentHeight;
}

WaveBand waveBand(float pitch, float waveHeight) {
  const float top = (pitch - waveHeight) * 0.5f;
  return WaveBand{top, top + waveHeight};
}

std::optional<std::uint32_t> waveHitAtY(double y, double firstLaneTop, double pitch, double waveTop,
                                        double waveBottom, std::uint32_t lanes) {
  const std::optional<std::uint32_t> lane = laneAtY(y, firstLaneTop, pitch, lanes);
  if (!lane) {
    return std::nullopt;
  }
  const double laneTop = firstLaneTop + pitch * static_cast<double>(*lane);
  if (y < laneTop + waveTop || y > laneTop + waveBottom) {
    return std::nullopt;
  }
  return lane;
}

double decoderLaneHeight(std::size_t shownRows, double rowPitch) {
  return static_cast<double>((std::max<std::size_t>)(shownRows, 1)) * rowPitch;
}

std::optional<std::size_t> decoderRowAtY(double y, double laneTop, double rowPitch, std::size_t shownRows) {
  if (rowPitch <= 0.0 || y < laneTop) return std::nullopt;
  const double row = std::floor((y - laneTop) / rowPitch);
  if (row >= static_cast<double>((std::max<std::size_t>)(shownRows, 1))) return std::nullopt;
  return static_cast<std::size_t>(row);
}

double stackedLaneTop(double stackTop, std::span<const double> laneHeights, std::size_t index) {
  double top = stackTop;
  for (std::size_t i = 0; i < index && i < laneHeights.size(); ++i) top += laneHeights[i];
  return top;
}

double stackedLanesHeight(std::span<const double> laneHeights) {
  double sum = 0.0;
  for (const double h : laneHeights) sum += h;
  return sum;
}

std::optional<std::size_t> laneAtYByHeights(double y, double firstLaneTop, std::span<const double> laneHeights) {
  if (y < firstLaneTop) return std::nullopt;
  double top = firstLaneTop;
  for (std::size_t i = 0; i < laneHeights.size(); ++i) {
    top += laneHeights[i];
    if (y < top) return i;
  }
  return std::nullopt;
}

}
