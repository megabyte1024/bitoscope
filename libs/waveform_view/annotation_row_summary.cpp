/*
 * SPDX-License-Identifier: MIT
 */

#include "annotation_row_summary.h"

#include <algorithm>
#include <map>
#include <utility>

namespace bitoscope::waveform_view {

namespace {

// WHY: quotient() repeats layoutAnnotationRow()'s expression on the same doubles: a column must not differ by
// rounding.
double quotient(std::uint64_t sample, double firstVisibleSample, double samplesPerPixel) {
  return (static_cast<double>(sample) - firstVisibleSample) / samplesPerPixel;
}

struct Edge {
  std::size_t next = 0;
  std::uint64_t covered = 0;
  double samples = 0.0;
  std::uint64_t nextBegin = 0;
};

std::size_t nextPast(AnnotationReader& reader, std::size_t g, std::size_t from, double x, double first, double spp,
                     std::size_t& probes) {
  // WHY: Search the begins, not the ends: spans are disjoint and in order, so only the span just before x can end
  // past it.
  const std::size_t past = reader.firstBeginNotBefore(
    g, from, [&](std::uint64_t begin) { return quotient(begin, first, spp) <= x; }, probes);
  if (past > from) {
    ++probes;
    if (quotient(reader.end(g, past - 1), first, spp) > x) return past - 1;
  }
  return past;
}

Edge edgeAt(AnnotationReader& reader, std::size_t g, const Edge& from, double x, double first, double spp,
            std::size_t& probes) {
  const AnnotationGroups& groups = reader.groups();
  const std::size_t n = groups.groupSize(g);
  Edge edge;
  edge.next = nextPast(reader, g, from.next, x, first, spp, probes);
  const std::size_t i = edge.next;
  const std::size_t stride = AnnotationGroups::kCoverageStride;
  const std::size_t checkpointReads = (std::min)(i % stride, (std::min)(i - i % stride + stride, n) - i);
  if (i - from.next <= checkpointReads) {
    edge.covered = from.covered;
    for (std::size_t j = from.next; j < i; ++j) edge.covered += reader.length(g, j);
    probes += i - from.next;
  } else {
    edge.covered = reader.coveredBefore(g, i, probes);
  }
  edge.samples = static_cast<double>(edge.covered);
  if (edge.next < n) {
    const auto [begin, length] = reader.span(g, edge.next);
    edge.nextBegin = begin;
    edge.samples += std::clamp(first + x * spp - static_cast<double>(begin), 0.0, static_cast<double>(length));
  }
  return edge;
}

std::vector<std::size_t> shownGroups(const AnnotationGroups& groups, std::uint32_t row,
                                     const DecoderVisibility& visibility) {
  std::vector<std::size_t> out;
  const auto [first, last] = groups.rowGroups(row);
  for (std::size_t g = first; g < last; ++g) {
    if (!classHidden(visibility, row, groups.groupClass(g))) out.push_back(g);
  }
  return out;
}

struct Track {
  std::size_t group;
  std::uint32_t cls;
  Edge edge;
};

RowSummary summarizeTracks(AnnotationReader& reader, std::vector<Track> tracks, double first, double spp,
                           std::uint32_t widthPixels) {
  const AnnotationGroups& groups = reader.groups();
  RowSummary out;
  if (tracks.empty()) return out;
  for (Track& track : tracks) track.edge = edgeAt(reader, track.group, Edge{}, 0.0, first, spp, out.probes);

  for (std::uint32_t c = 0; c < widthPixels; ++c) {
    const double right = static_cast<double>(c) + 1.0;
    std::optional<std::uint32_t> best;
    double bestSamples = 0.0;
    for (Track& track : tracks) {
      const Edge left = track.edge;
      track.edge = edgeAt(reader, track.group, left, right, first, spp, out.probes);
      const bool touched =
        left.next < groups.groupSize(track.group) && quotient(left.nextBegin, first, spp) < right;
      if (!touched) continue;
      const double samples = (std::max)(0.0, track.edge.samples - left.samples);
      if (!best || samples > bestSamples) {
        best = track.cls;
        bestSamples = samples;
      }
    }
    if (!best) continue;
    if (!out.runs.empty() && out.runs.back().lastColumn + 1 == c && out.runs.back().cls == *best) {
      out.runs.back().lastColumn = c;
    } else {
      out.runs.push_back({c, c, *best});
    }
  }
  return out;
}

}

RowSummary summarizeRow(const AnnotationGroups& groups, std::uint32_t row, const DecoderVisibility& visibility,
                        double firstVisibleSample, double samplesPerPixel, std::uint32_t widthPixels) {
  AnnotationReader reader(groups);
  return summarizeRow(reader, row, visibility, firstVisibleSample, samplesPerPixel, widthPixels);
}

RowSummary summarizeRow(AnnotationReader& reader, std::uint32_t row, const DecoderVisibility& visibility,
                        double firstVisibleSample, double samplesPerPixel, std::uint32_t widthPixels) {
  const AnnotationGroups& groups = reader.groups();
  if (!groups.coverageUsable() || widthPixels == 0 || !(samplesPerPixel > 0.0)) {
    return {};
  }
  std::vector<Track> tracks;
  for (const std::size_t g : shownGroups(groups, row, visibility)) tracks.push_back({g, groups.groupClass(g), {}});
  return summarizeTracks(reader, std::move(tracks), firstVisibleSample, samplesPerPixel, widthPixels);
}

RowSummary summarizeGroupsAs(const AnnotationGroups& groups, std::span<const std::size_t> groupNumbers,
                             std::uint32_t cls, double firstVisibleSample, double samplesPerPixel,
                             std::uint32_t widthPixels) {
  AnnotationReader reader(groups);
  return summarizeGroupsAs(reader, groupNumbers, cls, firstVisibleSample, samplesPerPixel, widthPixels);
}

RowSummary summarizeGroupsAs(AnnotationReader& reader, std::span<const std::size_t> groupNumbers, std::uint32_t cls,
                             double firstVisibleSample, double samplesPerPixel, std::uint32_t widthPixels) {
  if (!reader.groups().coverageUsable() || widthPixels == 0 || !(samplesPerPixel > 0.0)) return {};
  std::vector<Track> tracks;
  for (const std::size_t g : groupNumbers) tracks.push_back({g, cls, {}});
  return summarizeTracks(reader, std::move(tracks), firstVisibleSample, samplesPerPixel, widthPixels);
}

std::size_t firstSpanPast(const AnnotationGroups& groups, std::size_t g, double x, double firstVisibleSample,
                          double samplesPerPixel, std::size_t& probes) {
  AnnotationReader reader(groups);
  return firstSpanPast(reader, g, x, firstVisibleSample, samplesPerPixel, probes);
}

std::size_t firstSpanPast(AnnotationReader& reader, std::size_t g, double x, double firstVisibleSample,
                          double samplesPerPixel, std::size_t& probes) {
  const std::size_t past = reader.firstBeginNotBeforeByTable(
    g, [&](std::uint64_t begin) { return quotient(begin, firstVisibleSample, samplesPerPixel) <= x; }, probes);
  if (past > 0) {
    ++probes;
    if (quotient(reader.end(g, past - 1), firstVisibleSample, samplesPerPixel) > x) return past - 1;
  }
  return past;
}

std::optional<AnnotationRef> rowAnnotationAt(const AnnotationGroups& groups, std::uint32_t row,
                                             const DecoderVisibility& visibility, double firstVisibleSample,
                                             double samplesPerPixel, std::uint32_t widthPixels, std::uint32_t column) {
  AnnotationReader reader(groups);
  return rowAnnotationAt(reader, row, visibility, firstVisibleSample, samplesPerPixel, widthPixels, column);
}

std::optional<AnnotationRef> rowAnnotationAt(AnnotationReader& reader, std::uint32_t row,
                                             const DecoderVisibility& visibility, double firstVisibleSample,
                                             double samplesPerPixel, std::uint32_t widthPixels, std::uint32_t column) {
  const AnnotationGroups& groups = reader.groups();
  if (!groups.coverageUsable() || column >= widthPixels || !(samplesPerPixel > 0.0)) {
    return std::nullopt;
  }
  const double first = firstVisibleSample;
  const double spp = samplesPerPixel;
  const double left = static_cast<double>(column);
  const double right = left + 1.0;
  std::size_t probes = 0;
  std::optional<AnnotationRef> best;
  double bestSamples = 0.0;
  for (const std::size_t g : shownGroups(groups, row, visibility)) {
    // WHY: Hover repeats summarizeRow()'s arithmetic so it names the class drawn under the pointer.
    const Edge from = edgeAt(reader, g, Edge{}, left, first, spp, probes);
    if (from.next >= groups.groupSize(g) || !(quotient(from.nextBegin, first, spp) < right)) continue;
    const double samples = (std::max)(0.0, edgeAt(reader, g, from, right, first, spp, probes).samples - from.samples);
    if (!best || samples > bestSamples) {
      best = AnnotationRef{static_cast<std::uint32_t>(g), from.next};
      bestSamples = samples;
    }
  }
  return best;
}

std::vector<ColumnRun> withoutColumns(std::span<const ColumnRun> runs, std::span<const LaneTextMark> marks) {
  std::vector<ColumnRun> out;
  out.reserve(runs.size() + marks.size());
  std::size_t m = 0;
  for (const ColumnRun& run : runs) {
    std::uint32_t from = run.firstColumn;
    while (m < marks.size() && marks[m].lastColumn < from) ++m;
    bool open = true;
    for (std::size_t k = m; k < marks.size() && marks[k].firstColumn <= run.lastColumn; ++k) {
      if (marks[k].firstColumn > from) out.push_back({from, marks[k].firstColumn - 1, run.cls});
      if (marks[k].lastColumn >= run.lastColumn) {
        open = false;
        break;
      }
      from = (std::max)(from, marks[k].lastColumn + 1);
    }
    if (open) out.push_back({from, run.lastColumn, run.cls});
  }
  return out;
}

std::vector<ColumnRun> solidRuns(std::span<const ColumnRun> runs) {
  std::vector<ColumnRun> out;
  std::size_t i = 0;
  while (i < runs.size()) {
    std::size_t end = i + 1;
    while (end < runs.size() && runs[end].firstColumn == runs[end - 1].lastColumn + 1) ++end;
    std::map<std::uint32_t, std::uint64_t> columns;
    for (std::size_t k = i; k < end; ++k) {
      columns[runs[k].cls] += static_cast<std::uint64_t>(runs[k].lastColumn) - runs[k].firstColumn + 1;
    }
    std::uint32_t best = runs[i].cls;
    std::uint64_t most = 0;
    for (const auto& [cls, count] : columns) {
      if (count > most) {
        most = count;
        best = cls;
      }
    }
    out.push_back({runs[i].firstColumn, runs[end - 1].lastColumn, best});
    i = end;
  }
  return out;
}

std::vector<ColumnRun> solidBlocks(std::span<const CondensedMark> marks) {
  std::vector<ColumnRun> out;
  std::map<std::uint32_t, std::uint64_t> samples;
  std::uint32_t first = 0;
  std::uint32_t last = 0;
  const auto close = [&] {
    std::uint32_t best = samples.begin()->first;
    std::uint64_t most = 0;
    for (const auto& [cls, count] : samples) {
      if (count > most) {
        most = count;
        best = cls;
      }
    }
    out.push_back({first, last, best});
    samples.clear();
  };
  for (const CondensedMark& mark : marks) {
    if (!samples.empty() && static_cast<std::uint64_t>(mark.firstColumn) > static_cast<std::uint64_t>(last) + 1) close();
    if (samples.empty()) {
      first = mark.firstColumn;
      last = mark.lastColumn;
    } else {
      last = (std::max)(last, mark.lastColumn);
    }
    samples[mark.cls] += mark.samples;
  }
  if (!samples.empty()) close();
  return out;
}

}
