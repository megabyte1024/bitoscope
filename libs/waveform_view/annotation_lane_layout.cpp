/*
 * SPDX-License-Identifier: MIT
 */

#include "annotation_lane_layout.h"

#include <algorithm>
#include <cmath>

namespace bitoscope::waveform_view {

std::vector<AnnotationMark> layoutAnnotationLane(std::span<const bitoscope::decode::Annotation> annotations,
                                                 double firstVisibleSample, double samplesPerPixel,
                                                 std::uint32_t widthPixels, std::uint32_t minTextColumns) {
  std::vector<AnnotationMark> out;
  if (widthPixels == 0 || samplesPerPixel <= 0.0) return out;
  // WHY: Raw pointers in hot loops: a Debug build checks every vector or span access and ran several times slower.
  const double widthD = static_cast<double>(widthPixels);
  out.resize(annotations.size());
  AnnotationMark* const marks = out.data();
  const bitoscope::decode::Annotation* const src = annotations.data();
  std::size_t count = 0;
  for (std::size_t i = 0; i < annotations.size(); ++i) {
    const bitoscope::decode::Annotation& a = src[i];
    if (a.end <= a.begin) continue;
    const double firstCol = std::floor((static_cast<double>(a.begin) - firstVisibleSample) / samplesPerPixel);
    const double lastColExclusive = std::ceil((static_cast<double>(a.end) - firstVisibleSample) / samplesPerPixel);
    if (lastColExclusive <= 0.0 || firstCol >= widthD) continue;

    const double clippedFirst = firstCol > 0.0 ? firstCol : 0.0;
    const double clippedLastExclusive = lastColExclusive < widthD ? lastColExclusive : widthD;
    if (clippedLastExclusive <= clippedFirst) continue;

    const auto first = static_cast<std::uint32_t>(clippedFirst);
    const auto last = static_cast<std::uint32_t>(clippedLastExclusive) - 1;
    marks[count++] = AnnotationMark{i, first, last, (last - first + 1) < minTextColumns};
  }
  out.resize(count);

  std::vector<std::uint32_t> coverStorage(static_cast<std::size_t>(widthPixels) + 1, 0);
  std::uint32_t* const cover = coverStorage.data();
  for (std::size_t i = 0; i < count; ++i) {
    ++cover[marks[i].firstColumn];
    --cover[static_cast<std::size_t>(marks[i].lastColumn) + 1];
  }
  std::vector<std::uint32_t> sharedBeforeStorage(static_cast<std::size_t>(widthPixels) + 1, 0);
  std::uint32_t* const sharedBefore = sharedBeforeStorage.data();
  std::uint32_t covering = 0;
  for (std::size_t column = 0; column < widthPixels; ++column) {
    covering += cover[column];
    sharedBefore[column + 1] = sharedBefore[column] + (covering >= 2 ? 1u : 0u);
  }
  for (std::size_t i = 0; i < count; ++i) {
    if (sharedBefore[static_cast<std::size_t>(marks[i].lastColumn) + 1] > sharedBefore[marks[i].firstColumn]) {
      marks[i].dense = true;
    }
  }
  return out;
}

std::vector<std::size_t> marksToDraw(std::span<const AnnotationMark> marks, std::uint32_t widthPixels) {
  std::vector<std::size_t> out(marks.size());
  std::size_t* const kept = out.data();
  std::size_t keptCount = 0;
  const std::size_t tickCount = 2 * static_cast<std::size_t>(widthPixels) + 1;
  std::vector<unsigned char> tickDrawnStorage(tickCount, 0);
  unsigned char* const tickDrawn = tickDrawnStorage.data();
  const AnnotationMark* const all = marks.data();
  for (std::size_t i = 0; i < marks.size(); ++i) {
    const AnnotationMark& mark = all[i];
    if (mark.dense) {
      const std::size_t tick = static_cast<std::size_t>(mark.firstColumn) + mark.lastColumn;
      if (tick < tickCount) {
        if (tickDrawn[tick] != 0) continue;
        tickDrawn[tick] = 1;
      }
    }
    kept[keptCount++] = i;
  }
  out.resize(keptCount);
  return out;
}

std::optional<std::size_t> annotationMarkAtColumn(std::span<const AnnotationMark> marks,
                                                  std::uint32_t column) {
  for (const AnnotationMark& mark : marks) {
    if (column >= mark.firstColumn && column <= mark.lastColumn) return mark.annotationIndex;
  }
  return std::nullopt;
}

}
