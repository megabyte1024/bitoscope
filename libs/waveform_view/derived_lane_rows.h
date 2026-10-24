/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

#include "annotation_groups.h"
#include "annotation_lane_index.h"
#include "annotation_reader.h"
#include "annotation_row_summary.h"
#include "decoder.h"
#include "annotation_row_layout.h"
#include "decoder_visibility.h"
#include "derived_rows.h"

namespace bitoscope::waveform_view {

struct EdgeRule {
  decode::ClockEdges edges;
  const decode::SignalView* view = nullptr;
};

using DerivedRule = std::variant<decode::BitGrid, EdgeRule>;

struct DerivedRowSource {
  std::uint32_t row = 0;
  std::uint32_t cls = 0;
  std::vector<std::size_t> sourceGroups;
  DerivedRule rule;
};

struct DerivedRows {
  std::vector<DerivedRowSource> rows;
  mutable std::size_t wordsDerived = 0;
  const DerivedRowSource* find(std::uint32_t row) const;
};

std::uint32_t bitsPerWord(const DerivedRule& rule);

struct WordBits {
  std::array<decode::Annotation, decode::kMaxEdgeBits> bits{};
  std::uint32_t count = 0;
};

std::expected<void, std::string> deriveWord(const AnnotationGroups& groups, const DerivedRows& derived,
                                            const DerivedRowSource& source, std::size_t group, std::size_t index,
                                            WordBits& out);
std::expected<void, std::string> deriveWord(AnnotationReader& reader, const DerivedRows& derived,
                                            const DerivedRowSource& source, std::size_t group, std::size_t index,
                                            WordBits& out);

std::expected<void, std::string> appendDerivedBits(const AnnotationGroups& groups, const AnnotationRange& range,
                                                   const DerivedRows& derived, const DerivedRowSource& source,
                                                   std::uint64_t begin, std::uint64_t end,
                                                   std::vector<decode::Annotation>& out);
std::expected<void, std::string> appendDerivedBits(AnnotationReader& reader, const AnnotationRange& range,
                                                   const DerivedRows& derived, const DerivedRowSource& source,
                                                   std::uint64_t begin, std::uint64_t end,
                                                   std::vector<decode::Annotation>& out);

std::vector<RowClass> laneRowClassPairs(const AnnotationGroups& groups, const DerivedRows& derived);
std::vector<std::uint32_t> laneRowClasses(const AnnotationGroups& groups, const DerivedRows& derived,
                                          std::uint32_t row);

std::size_t laneRangeCount(const AnnotationGroups& groups, const AnnotationRange& range, const DerivedRows& derived,
                           std::uint64_t begin, std::uint64_t end);
std::size_t laneRangeCount(AnnotationReader& reader, const AnnotationRange& range, const DerivedRows& derived,
                           std::uint64_t begin, std::uint64_t end);

RowSummary summarizeLaneRow(const AnnotationGroups& groups, const DerivedRows& derived, std::uint32_t row,
                            const DecoderVisibility& visibility, double firstVisibleSample, double samplesPerPixel,
                            std::uint32_t widthPixels);
RowSummary summarizeLaneRow(AnnotationReader& reader, const DerivedRows& derived, std::uint32_t row,
                            const DecoderVisibility& visibility, double firstVisibleSample, double samplesPerPixel,
                            std::uint32_t widthPixels);

std::optional<decode::Annotation> laneRowAnnotationAt(const AnnotationGroups& groups, const DerivedRows& derived,
                                                      std::uint32_t row, const DecoderVisibility& visibility,
                                                      double firstVisibleSample, double samplesPerPixel,
                                                      std::uint32_t widthPixels, std::uint32_t column);
std::optional<decode::Annotation> laneRowAnnotationAt(AnnotationReader& reader, const DerivedRows& derived,
                                                      std::uint32_t row, const DecoderVisibility& visibility,
                                                      double firstVisibleSample, double samplesPerPixel,
                                                      std::uint32_t widthPixels, std::uint32_t column);

std::vector<RowLayout> layoutLaneRows(const AnnotationGroups& groups, const AnnotationRange& range,
                                      const DerivedRows& derived, std::span<const decode::AnnotationRow> rows,
                                      const DecoderVisibility& visibility, double firstVisibleSample,
                                      double samplesPerPixel, std::uint32_t widthPixels, std::uint64_t begin,
                                      std::uint64_t end);
std::vector<RowLayout> layoutLaneRows(AnnotationReader& reader, const AnnotationRange& range,
                                      const DerivedRows& derived, std::span<const decode::AnnotationRow> rows,
                                      const DecoderVisibility& visibility, double firstVisibleSample,
                                      double samplesPerPixel, std::uint32_t widthPixels, std::uint64_t begin,
                                      std::uint64_t end);

std::vector<LaneTextMark> rowTextMarks(const AnnotationGroups& groups, std::span<const LaneTextMark> marks,
                                       std::uint32_t row);

}
