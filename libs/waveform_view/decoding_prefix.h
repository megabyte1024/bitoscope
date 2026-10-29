/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <optional>

namespace bitoscope::waveform_view {

enum class LaneDrawing { Nothing, Result, Prefix };

LaneDrawing laneDrawing(bool resultDrawn, bool prefixHeld);

struct HatchedColumns {
  std::uint32_t first = 0;
  std::uint32_t end = 0;
  bool operator==(const HatchedColumns&) const = default;
};

std::optional<HatchedColumns> hatchedColumns(LaneDrawing drawing, std::uint64_t watermark, double firstVisibleSample,
                                             double samplesPerPixel, std::uint32_t columnCount);

bool laneColumnHittable(const std::optional<HatchedColumns>& hatched, double column);

enum class LaneStatusKind { None, Decoding, DecodingPercent, StartsWhenCaptureEnds, Failed };

enum class LaneCaptureMode { Ended, Live, StartsAtEnd };

struct LaneStatus {
  LaneStatusKind kind = LaneStatusKind::None;
  int percent = 0;
  bool operator==(const LaneStatus&) const = default;
};

LaneStatus decoderLaneStatus(bool decoding, bool failed, bool resultDrawn, std::optional<int> percent,
                             LaneCaptureMode mode = LaneCaptureMode::Ended);

}
