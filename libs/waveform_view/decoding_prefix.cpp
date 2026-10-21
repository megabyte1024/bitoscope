/*
 * SPDX-License-Identifier: MIT
 */

#include "decoding_prefix.h"

#include <cmath>

namespace bitoscope::waveform_view {

LaneDrawing laneDrawing(bool resultDrawn, bool prefixHeld) {
  if (resultDrawn) return LaneDrawing::Result;
  if (prefixHeld) return LaneDrawing::Prefix;
  return LaneDrawing::Nothing;
}

std::optional<HatchedColumns> hatchedColumns(LaneDrawing drawing, std::uint64_t watermark, double firstVisibleSample,
                                             double samplesPerPixel, std::uint32_t columnCount) {
  if (drawing != LaneDrawing::Prefix) return std::nullopt;
  const double column = std::floor((static_cast<double>(watermark) - firstVisibleSample) / samplesPerPixel);
  if (column >= static_cast<double>(columnCount)) return std::nullopt;
  return HatchedColumns{column <= 0.0 ? 0u : static_cast<std::uint32_t>(column), columnCount};
}

bool laneColumnHittable(const std::optional<HatchedColumns>& hatched, double column) {
  return !hatched || column < static_cast<double>(hatched->first);
}

LaneStatus decoderLaneStatus(bool decoding, bool failed, bool resultDrawn, std::optional<int> percent) {
  if (failed) return {LaneStatusKind::Failed, 0};
  if (!decoding) return {};
  if (percent) return {LaneStatusKind::DecodingPercent, *percent};
  if (!resultDrawn) return {LaneStatusKind::Decoding, 0};
  return {};
}

}
