/*
 * SPDX-License-Identifier: MIT
 */

#include "decoder_row_strip.h"

#include <algorithm>
#include <cmath>

#include "annotation_role.h"

namespace bitoscope::waveform_view {

std::vector<RowLayout> layoutAnnotationRows(const AnnotationGroups& groups, const AnnotationRange& range,
                                            std::span<const decode::AnnotationRow> rows,
                                            const DecoderVisibility& visibility, double firstVisibleSample,
                                            double samplesPerPixel, std::uint32_t widthPixels) {
  AnnotationReader reader(groups);
  return layoutAnnotationRows(reader, range, rows, visibility, firstVisibleSample, samplesPerPixel, widthPixels);
}

std::vector<RowLayout> layoutAnnotationRows(AnnotationReader& reader, const AnnotationRange& range,
                                            std::span<const decode::AnnotationRow> rows,
                                            const DecoderVisibility& visibility, double firstVisibleSample,
                                            double samplesPerPixel, std::uint32_t widthPixels) {
  const AnnotationGroups& groups = reader.groups();
  std::vector<RowLayout> layouts(rows.size());
  if (!(samplesPerPixel > 0.0) || widthPixels == 0) return layouts;
  for (std::size_t slot = 0; slot < rows.size(); ++slot) {
    const std::uint32_t row = rows[slot].id;
    if (rowHidden(visibility, row) || rowSlot(rows, row) != slot) continue;
    std::vector<std::size_t> shown;
    const auto [first, last] = groups.rowGroups(row);
    for (std::size_t g = first; g < last; ++g) {
      if (!classHidden(visibility, row, groups.groupClass(g))) shown.push_back(g);
    }
    if (shown.empty()) continue;
    RowLayout& layout = layouts[slot];
    layout.annotations = collect(reader, range, shown);
    layout.marks = layoutAnnotationRow(layout.annotations, row, firstVisibleSample, samplesPerPixel, widthPixels);
  }
  return layouts;
}

std::string classDisplayName(const decode::AnnotationClass& cls) {
  if (!cls.name.empty()) return cls.name;
  if (cls.role == decode::AnnotationRole::None) return std::to_string(cls.id);
  std::string word(decode::annotationRoleName(cls.role));
  if (!word.empty() && word[0] >= 'a' && word[0] <= 'z') word[0] = static_cast<char>(word[0] - 'a' + 'A');
  return word;
}

std::vector<StripClass> rowStripClasses(std::span<const std::uint32_t> presentIds,
                                        std::span<const decode::AnnotationClass> classes) {
  std::vector<StripClass> out;
  for (const decode::AnnotationClass& cls : classes) {
    if (std::ranges::find(presentIds, cls.id) != presentIds.end()) out.push_back({cls.id, classDisplayName(cls)});
  }
  std::vector<std::uint32_t> unknown;
  for (const std::uint32_t id : presentIds) {
    if (std::ranges::none_of(classes, [&](const decode::AnnotationClass& c) { return c.id == id; })) unknown.push_back(id);
  }
  std::ranges::sort(unknown);
  unknown.erase(std::unique(unknown.begin(), unknown.end()), unknown.end());
  for (const std::uint32_t id : unknown) out.push_back({id, std::to_string(id)});
  return out;
}

DecoderRowSpace decoderRowSpace(bool rowHidden, bool stripOpen, double rowPitch, double stripHeight) {
  const double band = rowHidden ? std::round(rowPitch * kHiddenRowPitchFraction) : rowPitch;
  return {band, stripOpen ? stripHeight : 0.0};
}

std::vector<double> decoderRowHeights(std::span<const DecoderRowSpace> rows) {
  std::vector<double> heights;
  heights.reserve(rows.size());
  for (const DecoderRowSpace& row : rows) heights.push_back(row.height());
  return heights;
}

std::optional<DecoderRowHit> decoderRowHitAtY(double y, double laneTop, std::span<const DecoderRowSpace> rows) {
  if (y < laneTop) return std::nullopt;
  double top = laneTop;
  for (std::size_t slot = 0; slot < rows.size(); ++slot) {
    const double bandBottom = top + rows[slot].band;
    const double bottom = bandBottom + rows[slot].strip;
    if (y < bottom) return DecoderRowHit{slot, !(y < bandBottom)};
    top = bottom;
  }
  return std::nullopt;
}

RowStripLayout layoutRowStrip(std::span<const double> classItemWidths, double width, double gap, double linePitch,
                              double padding, bool formatLine) {
  RowStripLayout layout;
  if (formatLine) layout.formatLine = 1;
  const double right = width - padding;
  double x = padding;
  std::size_t line = layout.formatLine.value_or(0);
  for (const double itemWidth : classItemWidths) {
    if (layout.classItems.empty() || x + itemWidth > right) {
      ++line;
      x = padding;
    }
    layout.classItems.push_back({x, line});
    x += itemWidth + gap;
  }
  layout.lines = line + 1;
  layout.height = 2.0 * padding + static_cast<double>(layout.lines) * linePitch;
  return layout;
}

}
