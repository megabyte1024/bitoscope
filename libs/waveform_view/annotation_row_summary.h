/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "annotation_groups.h"
#include "annotation_lane_index.h"
#include "annotation_reader.h"
#include "decoder_visibility.h"
#include "lane_coverage.h"

namespace bitoscope::waveform_view {

struct ColumnRun {
  std::uint32_t firstColumn = 0;
  std::uint32_t lastColumn = 0;
  std::uint32_t cls = 0;
  bool operator==(const ColumnRun&) const = default;
};

struct RowSummary {
  std::vector<ColumnRun> runs;
  std::size_t probes = 0;
  std::size_t decided = 0;
};

RowSummary summarizeRow(const AnnotationGroups& groups, std::uint32_t row, const DecoderVisibility& visibility,
                        double firstVisibleSample, double samplesPerPixel, std::uint32_t widthPixels);
RowSummary summarizeRow(AnnotationReader& reader, std::uint32_t row, const DecoderVisibility& visibility,
                        double firstVisibleSample, double samplesPerPixel, std::uint32_t widthPixels);

std::optional<AnnotationRef> rowAnnotationAt(const AnnotationGroups& groups, std::uint32_t row,
                                             const DecoderVisibility& visibility, double firstVisibleSample,
                                             double samplesPerPixel, std::uint32_t widthPixels, std::uint32_t column);
std::optional<AnnotationRef> rowAnnotationAt(AnnotationReader& reader, std::uint32_t row,
                                             const DecoderVisibility& visibility, double firstVisibleSample,
                                             double samplesPerPixel, std::uint32_t widthPixels, std::uint32_t column);

RowSummary summarizeGroupsAs(const AnnotationGroups& groups, std::span<const std::size_t> groupNumbers,
                             std::uint32_t cls, double firstVisibleSample, double samplesPerPixel,
                             std::uint32_t widthPixels);
RowSummary summarizeGroupsAs(AnnotationReader& reader, std::span<const std::size_t> groupNumbers, std::uint32_t cls,
                             double firstVisibleSample, double samplesPerPixel, std::uint32_t widthPixels);
RowSummary summarizeRow(AnnotationReader& reader, const LaneCoverage& coverage, std::uint32_t row,
                        const DecoderVisibility& visibility, double firstVisibleSample, double samplesPerPixel,
                        std::uint32_t widthPixels);
RowSummary summarizeGroupsAs(AnnotationReader& reader, const LaneCoverage& coverage,
                             std::span<const std::size_t> groupNumbers, std::uint32_t cls, double firstVisibleSample,
                             double samplesPerPixel, std::uint32_t widthPixels);

std::size_t firstSpanPast(const AnnotationGroups& groups, std::size_t g, double x, double firstVisibleSample,
                          double samplesPerPixel, std::size_t& probes);
std::size_t firstSpanPast(AnnotationReader& reader, std::size_t g, double x, double firstVisibleSample,
                          double samplesPerPixel, std::size_t& probes);

std::vector<ColumnRun> withoutColumns(std::span<const ColumnRun> runs, std::span<const LaneTextMark> marks);

std::vector<ColumnRun> solidRuns(std::span<const ColumnRun> runs);

struct CondensedMark {
  std::uint32_t firstColumn = 0;
  std::uint32_t lastColumn = 0;
  std::uint32_t cls = 0;
  std::uint64_t samples = 0;
};

std::vector<ColumnRun> solidBlocks(std::span<const CondensedMark> marks);

}
