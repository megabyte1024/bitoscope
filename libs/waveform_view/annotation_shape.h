/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "annotation_colors.h"
#include "annotation_lane_index.h"
#include "decoder.h"
#include "viewport.h"

namespace bitoscope::waveform_view {

struct ShapePoint {
  double x = 0.0;
  double y = 0.0;
  bool operator==(const ShapePoint&) const = default;
};

inline constexpr double kMinHexagonWidth = 4.0;

// WHY: Each hexagon point takes at most a quarter of the width: flat top and bottom keep half, so it never
// closes into a diamond.
inline constexpr double kHexagonMaxPointFraction = 0.25;

inline constexpr double kHexagonMaxPointEm = 0.35;

struct AnnotationShape {
  std::array<ShapePoint, 6> points{};
  std::size_t count = 0;
  double depth = 0.0;
};

double hexagonPointDepth(double width, double rowHeight, double maxPointDepth);

AnnotationShape annotationShape(double x0, double x1, double top, double bottom, double maxPointDepth);

struct AnnotationSpan {
  double x0 = 0.0;
  double x1 = 0.0;
  bool operator==(const AnnotationSpan&) const = default;
};
AnnotationSpan annotationSpanColumns(const Viewport& columns, std::uint64_t begin, std::uint64_t end,
                                     double widthColumns);

std::optional<double> annotationTextLeft(double x0, double x1, double depth, double textWidth, double viewLeft,
                                         double viewRight);

// WHY: A span cut at the view edge keeps its point 64 columns outside: a cut span must not show a point inside
// as if it ended there.
inline constexpr double kRowMarkOverhangColumns = 64.0;

struct RowMark {
  std::size_t annotationIndex = 0;
  double x0 = 0.0;
  double x1 = 0.0;
  bool tick = false;
  bool operator==(const RowMark&) const = default;
};

std::vector<RowMark> layoutAnnotationRow(std::span<const decode::Annotation> annotations, std::uint32_t row,
                                         double firstVisibleSample, double samplesPerPixel,
                                         std::uint32_t widthPixels);

std::optional<std::size_t> rowMarkAtColumn(std::span<const RowMark> marks, double column);

std::vector<decode::AnnotationRow> shownDecoderRows(std::span<const decode::AnnotationRow> declared,
                                                    std::span<const std::uint32_t> emittedRowIds);

std::optional<std::size_t> rowSlot(std::span<const decode::AnnotationRow> rows, std::uint32_t rowId);

std::string decoderRowHeader(std::string_view laneLabel, std::string_view rowName, std::size_t shownRows);

struct ClassFill {
  std::uint32_t row = 0;
  std::uint32_t cls = 0;
  Rgb8 fill;
  bool operator==(const ClassFill&) const = default;
};

std::vector<ClassFill> annotationClassFills(std::span<const decode::AnnotationRow> rows,
                                            std::span<const RowClass> rowClasses,
                                            std::span<const decode::AnnotationClass> classes,
                                            bool dark, Rgb8 neutral);

Rgb8 classFill(std::span<const ClassFill> fills, std::uint32_t row, std::uint32_t cls, Rgb8 neutral);

Rgb8 annotationOutlineColor(Rgb8 fill, bool dark);

}
