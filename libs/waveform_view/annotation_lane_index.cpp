/*
 * SPDX-License-Identifier: MIT
 */

#include "annotation_lane_index.h"

#include <algorithm>
#include <cmath>

namespace bitoscope::waveform_view {

namespace {

// WHY: Same quotient as layoutAnnotationLane() on the same doubles: a rank and a column must not differ by rounding.
double quotient(std::uint64_t sample, double firstVisibleSample, double samplesPerPixel) {
  return (static_cast<double>(sample) - firstVisibleSample) / samplesPerPixel;
}

template <class Before>
std::size_t firstNotBefore(std::size_t from, std::size_t n, Before before) {
  if (from >= n || !before(from)) return from;
  std::size_t lo = from;
  std::size_t step = 1;
  std::size_t hi = lo + step;
  while (hi < n && before(hi)) {
    lo = hi;
    step *= 2;
    hi = lo + step;
  }
  if (hi > n) hi = n;
  while (hi - lo > 1) {
    const std::size_t mid = lo + (hi - lo) / 2;
    if (before(mid)) {
      lo = mid;
    } else {
      hi = mid;
    }
  }
  return hi;
}

template <class Groups>
std::optional<AnnotationRef> firstEndingPast(Groups& groups, double column, double first, double spp) {
  return firstEndingAfter(groups, [&](std::uint64_t end) { return quotient(end, first, spp) > column; });
}

}

bool laneNeedsSummary(std::size_t annotationsInView, std::uint32_t widthPixels) {
  return annotationsInView > kLaneSummaryAnnotationsPerColumn * static_cast<std::size_t>(widthPixels);
}

AnnotationLaneSummary summarizeLane(const AnnotationGroups& groups, double firstVisibleSample, double samplesPerPixel,
                                    std::uint32_t widthPixels, std::uint32_t minTextColumns) {
  AnnotationReader reader(groups);
  return summarizeLane(reader, firstVisibleSample, samplesPerPixel, widthPixels, minTextColumns);
}

AnnotationLaneSummary summarizeLane(AnnotationReader& reader, double firstVisibleSample, double samplesPerPixel,
                                    std::uint32_t widthPixels, std::uint32_t minTextColumns) {
  const AnnotationGroups& groups = reader.groups();
  AnnotationLaneSummary out;
  if (!groups.usable() || widthPixels == 0 || samplesPerPixel <= 0.0 || groups.size() == 0) return out;
  const std::size_t width = widthPixels;
  const double first = firstVisibleSample;
  const double spp = samplesPerPixel;

  // WHY: Raw pointers in loops: a Debug build checks every vector access and ran these passes several times slower.
  std::vector<std::size_t> startedStorage(width + 1, 0);
  std::vector<std::size_t> endedStorage(width + 1, 0);
  std::size_t* const started = startedStorage.data();
  std::size_t* const ended = endedStorage.data();
  std::size_t probes = 0;
  for (std::size_t g = 0; g < groups.groupCount(); ++g) {
    const std::size_t n = groups.groupSize(g);
    const bool regular = groups.groupRegular(g);
    std::size_t atStart = 0;
    std::size_t atEnd = 0;
    for (std::size_t k = 0; k <= width; ++k) {
      const double column = static_cast<double>(k);
      atStart = reader.firstBeginNotBefore(
        g, atStart, [&](std::uint64_t begin) { return quotient(begin, first, spp) < column; }, probes);
      if (regular) {
        // WHY: No search of ends: in a regular group spans ending by the edge are those beginning by it,
        // less the last one when it reaches past the edge.
        const std::size_t past = reader.firstBeginNotBefore(
          g, atStart, [&](std::uint64_t begin) { return quotient(begin, first, spp) <= column; }, probes);
        atEnd = past > 0 && quotient(reader.end(g, past - 1), first, spp) > column ? past - 1 : past;
      } else {
        atEnd = firstNotBefore(atEnd, n, [&](std::size_t i) { return quotient(groups.sortedEnd(g, i), first, spp) <= column; });
      }
      started[k] += atStart;
      ended[k] += atEnd;
    }
  }

  const auto touching = [&](std::size_t c) { return static_cast<std::ptrdiff_t>(started[c + 1] - ended[c]); };
  const auto beginning = [&](std::size_t c) {
    return c == 0 ? touching(0) : static_cast<std::ptrdiff_t>(started[c + 1] - started[c]);
  };
  const auto finishing = [&](std::size_t c) {
    return c + 1 == width ? touching(c) : static_cast<std::ptrdiff_t>(ended[c + 1] - ended[c]);
  };

  std::size_t c = 0;
  while (c < width) {
    const std::ptrdiff_t here = touching(c);
    if (here <= 0) {
      ++c;
      continue;
    }
    if (here >= 2) {
      out.tickColumns.push_back(static_cast<std::uint32_t>(c));
      ++c;
      continue;
    }
    const std::size_t runFirst = c;
    std::size_t runLast = c;
    while (runLast + 1 < width && touching(runLast + 1) == 1 && beginning(runLast + 1) == 0) ++runLast;
    c = runLast + 1;
    const bool whole = beginning(runFirst) > 0 && finishing(runLast) > 0;
    if (whole && (runLast - runFirst + 1) >= minTextColumns) {
      if (const auto which = firstEndingPast(reader, static_cast<double>(runFirst), first, spp)) {
        out.textMarks.push_back(LaneTextMark{*which, static_cast<std::uint32_t>(runFirst),
                                             static_cast<std::uint32_t>(runLast)});
        continue;
      }
    }
    for (std::size_t x = runFirst; x <= runLast; ++x) out.tickColumns.push_back(static_cast<std::uint32_t>(x));
  }
  return out;
}

std::optional<AnnotationRef> annotationAtColumn(const AnnotationGroups& groups, double firstVisibleSample,
                                                double samplesPerPixel, std::uint32_t widthPixels,
                                                std::uint32_t column) {
  AnnotationReader reader(groups);
  return annotationAtColumn(reader, firstVisibleSample, samplesPerPixel, widthPixels, column);
}

std::optional<AnnotationRef> annotationAtColumn(AnnotationReader& reader, double firstVisibleSample,
                                                double samplesPerPixel, std::uint32_t widthPixels,
                                                std::uint32_t column) {
  if (!reader.groups().usable() || column >= widthPixels || samplesPerPixel <= 0.0) return std::nullopt;
  const double c = static_cast<double>(column);
  const auto which = firstEndingPast(reader, c, firstVisibleSample, samplesPerPixel);
  if (!which) return std::nullopt;
  if (quotient(reader.begin(which->group, which->index), firstVisibleSample, samplesPerPixel) < c + 1.0) return which;
  return std::nullopt;
}

}
