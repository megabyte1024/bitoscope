/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "decoder.h"

namespace bitoscope::waveform_view {

struct AnnotationMark {
  std::size_t annotationIndex = 0;
  std::uint32_t firstColumn = 0;
  std::uint32_t lastColumn = 0;
  bool dense = false;
  bool operator==(const AnnotationMark&) const = default;
};

std::vector<AnnotationMark> layoutAnnotationLane(std::span<const bitoscope::decode::Annotation> annotations,
                                                 double firstVisibleSample, double samplesPerPixel,
                                                 std::uint32_t widthPixels,
                                                 std::uint32_t minTextColumns = 8);

std::vector<std::size_t> marksToDraw(std::span<const AnnotationMark> marks, std::uint32_t widthPixels);

std::optional<std::size_t> annotationMarkAtColumn(std::span<const AnnotationMark> marks,
                                                  std::uint32_t column);

}
