/*
 * SPDX-License-Identifier: MIT
 */

#include "annotation_row_layout.h"

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
    if (rowSlot(rows, row) != slot) continue;
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

}
