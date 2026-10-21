/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "annotation_groups.h"
#include "annotation_lane_layout.h"
#include "annotation_reader.h"

namespace bitoscope::waveform_view {

inline constexpr std::size_t kLaneSummaryAnnotationsPerColumn = 4;

bool laneNeedsSummary(std::size_t annotationsInView, std::uint32_t widthPixels);

struct LaneTextMark {
  AnnotationRef annotation;
  std::uint32_t firstColumn = 0;
  std::uint32_t lastColumn = 0;
  bool operator==(const LaneTextMark&) const = default;
};

struct AnnotationLaneSummary {
  std::vector<LaneTextMark> textMarks;
  std::vector<std::uint32_t> tickColumns;
};

AnnotationLaneSummary summarizeLane(const AnnotationGroups& groups, double firstVisibleSample, double samplesPerPixel,
                                    std::uint32_t widthPixels, std::uint32_t minTextColumns = 8);
AnnotationLaneSummary summarizeLane(AnnotationReader& reader, double firstVisibleSample, double samplesPerPixel,
                                    std::uint32_t widthPixels, std::uint32_t minTextColumns = 8);

std::optional<AnnotationRef> annotationAtColumn(const AnnotationGroups& groups, double firstVisibleSample,
                                                double samplesPerPixel, std::uint32_t widthPixels,
                                                std::uint32_t column);
std::optional<AnnotationRef> annotationAtColumn(AnnotationReader& reader, double firstVisibleSample,
                                                double samplesPerPixel, std::uint32_t widthPixels,
                                                std::uint32_t column);

}
