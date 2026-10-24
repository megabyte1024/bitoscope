/*
 * SPDX-License-Identifier: MIT
 */

#include "annotation_shape.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace bitoscope::waveform_view {

double hexagonPointDepth(double width, double rowHeight, double maxPointDepth) {
  const double widest = (std::min)(rowHeight / 2.0, (std::max)(0.0, maxPointDepth));
  const double ramp = (width - kMinHexagonWidth) * kHexagonMaxPointFraction;
  return (std::max)(0.0, (std::min)(widest, ramp));
}

AnnotationShape annotationShape(double x0, double x1, double top, double bottom, double maxPointDepth) {
  AnnotationShape shape;
  const double width = x1 - x0;
  const double height = bottom - top;
  if (!(width > 0.0) || !(height > 0.0)) return shape;
  const double depth = hexagonPointDepth(width, height, maxPointDepth);
  if (depth <= 0.0) {
    shape.points = {{{x0, top}, {x1, top}, {x1, bottom}, {x0, bottom}}};
    shape.count = 4;
    return shape;
  }
  const double mid = top + height / 2.0;
  shape.depth = depth;
  shape.points = {{{x0, mid}, {x0 + depth, top}, {x1 - depth, top}, {x1, mid}, {x1 - depth, bottom}, {x0 + depth, bottom}}};
  shape.count = 6;
  return shape;
}

AnnotationSpan annotationSpanColumns(const Viewport& columns, std::uint64_t begin, std::uint64_t end,
                                     double widthColumns) {
  const double lowest = -kRowMarkOverhangColumns;
  const double highest = widthColumns + kRowMarkOverhangColumns;
  const auto edge = [&](std::uint64_t sample) {
    // WHY: Far ends are decided from the fractional position: an end that far away is held at the overhang whatever
    // column holds it.
    const double fractional = columns.sampleToPixel(static_cast<double>(sample));
    if (fractional <= lowest - 1.0) return lowest;
    if (fractional >= highest + 1.0) return highest;
    return (std::clamp)(columns.samplePixelX(static_cast<std::int64_t>(sample)), lowest, highest);
  };
  AnnotationSpan span{edge(begin), edge(end)};
  if (span.x1 - span.x0 < 1.0) span.x1 = span.x0 + 1.0;
  return span;
}

std::optional<double> annotationTextLeft(double x0, double x1, double depth, double textWidth, double viewLeft,
                                         double viewRight) {
  const double left = (std::max)(x0 + depth, viewLeft);
  const double right = (std::min)(x1 - depth, viewRight);
  if (right - left < textWidth) return std::nullopt;
  return (left + right - textWidth) / 2.0;
}

std::vector<RowMark> layoutAnnotationRow(std::span<const decode::Annotation> annotations, std::uint32_t row,
                                         double firstVisibleSample, double samplesPerPixel,
                                         std::uint32_t widthPixels) {
  std::vector<RowMark> marks;
  if (!(samplesPerPixel > 0.0) || widthPixels == 0) return marks;
  const double width = static_cast<double>(widthPixels);
  const double lowest = -kRowMarkOverhangColumns;
  const double highest = width + kRowMarkOverhangColumns;
  std::optional<double> lastTick;
  for (std::size_t i = 0; i < annotations.size(); ++i) {
    const decode::Annotation& a = annotations[i];
    if (a.row != row) continue;
    const double x0 = (static_cast<double>(a.begin) - firstVisibleSample) / samplesPerPixel;
    const double x1 = (static_cast<double>(a.end) - firstVisibleSample) / samplesPerPixel;
    if (!(x1 > 0.0) || !(x0 < width) || !(x1 > x0)) continue;
    if (x1 - x0 < 1.0) {
      const double column = std::clamp(std::floor((x0 + x1) / 2.0), 0.0, width - 1.0);
      if (lastTick && *lastTick == column) continue;
      lastTick = column;
      marks.push_back({i, column, column + 1.0, true});
      continue;
    }
    marks.push_back({i, (std::max)(x0, lowest), (std::min)(x1, highest), false});
  }
  return marks;
}

std::optional<std::size_t> rowMarkAtColumn(std::span<const RowMark> marks, double column) {
  for (const RowMark& mark : marks) {
    if (column >= mark.x0 && column < mark.x1) return mark.annotationIndex;
  }
  return std::nullopt;
}

std::vector<decode::AnnotationRow> shownDecoderRows(std::span<const decode::AnnotationRow> declared,
                                                    std::span<const std::uint32_t> emittedRowIds) {
  if (!declared.empty()) return {declared.begin(), declared.end()};
  std::set<std::uint32_t> ids(emittedRowIds.begin(), emittedRowIds.end());
  if (ids.empty()) ids.insert(0);
  std::vector<decode::AnnotationRow> rows;
  rows.reserve(ids.size());
  for (const std::uint32_t id : ids) rows.push_back({id, {}});
  return rows;
}

std::optional<std::size_t> rowSlot(std::span<const decode::AnnotationRow> rows, std::uint32_t rowId) {
  for (std::size_t i = 0; i < rows.size(); ++i) {
    if (rows[i].id == rowId) return i;
  }
  return std::nullopt;
}

std::string decoderRowHeader(std::string_view laneLabel, std::string_view rowName, std::size_t shownRows) {
  if (shownRows <= 1 || rowName.empty()) return std::string(laneLabel);
  return std::string(rowName);
}

std::vector<ClassFill> annotationClassFills(std::span<const decode::AnnotationRow> rows,
                                            std::span<const RowClass> rowClasses,
                                            std::span<const decode::AnnotationClass> classes,
                                            bool dark, Rgb8 neutral) {
  std::vector<ClassFill> fills;
  fills.reserve(rows.size() * classes.size());
  for (const decode::AnnotationRow& row : rows) {
    for (const decode::AnnotationClass& cls : classes) {
      const std::size_t variant = annotationColorVariant(rows, rowClasses, classes, row.id, cls.role);
      fills.push_back({row.id, cls.id, defaultAnnotationColor(cls.role, dark, neutral, variant)});
    }
  }
  return fills;
}

Rgb8 classFill(std::span<const ClassFill> fills, std::uint32_t row, std::uint32_t cls, Rgb8 neutral) {
  for (const ClassFill& fill : fills) {
    if (fill.row == row && fill.cls == cls) return fill.fill;
  }
  return neutral;
}

Rgb8 annotationOutlineColor(Rgb8 fill, bool dark) {
  constexpr double kTowardInk = 0.45;
  const Rgb8 ink = annotationTextColor(dark);
  const auto mix = [&](std::uint8_t from, std::uint8_t to) {
    return static_cast<std::uint8_t>(std::lround(from + (static_cast<double>(to) - from) * kTowardInk));
  };
  return {mix(fill.r, ink.r), mix(fill.g, ink.g), mix(fill.b, ink.b)};
}

}
