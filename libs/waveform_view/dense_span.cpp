/*
 * SPDX-License-Identifier: MIT
 */

#include "dense_span.h"

#include <algorithm>
#include <cmath>
#include <optional>

#include "channel_render.h"
#include "viewport.h"
#include "waveform_source.h"

namespace bitoscope::waveform_view {

namespace {

using Candidate = DenseProbe;

std::optional<bool> blocksHold(const WaveformSource& source, uint32_t channel, uint64_t begin,
                               uint64_t end, bool wanted) {
  const std::optional<ColumnSummary> summary = source.blockRange(channel, begin, end);
  if (!summary.has_value()) return std::nullopt;
  return wanted ? summary->has_one : summary->has_zero;
}

std::optional<bool> flatBlockLevel(const WaveformSource& source, uint32_t channel, uint64_t begin,
                                   uint64_t end) {
  const std::optional<ColumnSummary> summary = source.blockRange(channel, begin, end);
  if (!summary.has_value() || (summary->has_zero && summary->has_one)) return std::nullopt;
  return summary->has_one;
}

std::optional<uint64_t> blockWith(const WaveformSource& source, uint32_t channel, const BlockGrid& grid,
                                  uint64_t begin, uint64_t end, bool wanted, bool last) {
  if (grid.samples == 0 || end <= begin) return std::nullopt;
  const std::optional<bool> any = blocksHold(source, channel, begin, end, wanted);
  if (!any.has_value() || !*any) return std::nullopt;
  uint64_t low = begin;
  uint64_t high = end;
  while (high - low > grid.samples) {
    const uint64_t blocks = (high - low) / grid.samples;
    if (last) {
      const uint64_t middle = low + (blocks + 1) / 2 * grid.samples;
      const std::optional<bool> right = blocksHold(source, channel, middle, high, wanted);
      if (!right.has_value()) return std::nullopt;
      if (*right) {
        low = middle;
      } else {
        high = middle;
      }
      continue;
    }
    const uint64_t middle = low + blocks / 2 * grid.samples;
    const std::optional<bool> left = blocksHold(source, channel, low, middle, wanted);
    if (!left.has_value()) return std::nullopt;
    if (*left) {
      high = middle;
    } else {
      low = middle;
    }
  }
  return low;
}

struct OpenSides {
  bool before = false;
  bool after = false;
};

std::optional<Candidate> blockCandidate(const WaveformSource& source, uint32_t channel,
                                        const BlockGrid& grid, uint64_t firstBegin, uint64_t firstEnd,
                                        uint64_t lastBegin, uint64_t lastEnd, uint64_t total, OpenSides open) {
  std::optional<bool> before;
  std::optional<bool> after;
  if (!open.before) {
    const uint64_t beforeEnd = grid.lastBlockEndBy(firstBegin);
    if (beforeEnd < grid.samples) return std::nullopt;
    before = flatBlockLevel(source, channel, beforeEnd - grid.samples, beforeEnd);
    if (!before.has_value()) return std::nullopt;
  }
  if (!open.after) {
    const uint64_t afterBegin = grid.firstBlockFrom(lastEnd);
    if (afterBegin + grid.samples > total) return std::nullopt;
    after = flatBlockLevel(source, channel, afterBegin, afterBegin + grid.samples);
    if (!after.has_value()) return std::nullopt;
  }

  // WHY: Search starts at the block HOLDING the column's first sample: an end can lie in the block straddling the
  // column start.
  const std::optional<uint64_t> firstFrom = grid.blockHolding(firstBegin);
  const std::optional<uint64_t> firstTo = grid.blockHolding(firstEnd - 1);
  const std::optional<uint64_t> lastFrom = grid.blockHolding(lastBegin);
  const std::optional<uint64_t> lastTo = grid.blockHolding(lastEnd - 1);
  if (!firstFrom.has_value() || !firstTo.has_value() || !lastFrom.has_value() || !lastTo.has_value()) {
    return std::nullopt;
  }
  const uint64_t firstRangeEnd = (std::min)(*firstTo + grid.samples, total);
  const uint64_t lastRangeEnd = (std::min)(*lastTo + grid.samples, total);
  constexpr bool findFirstBlock = false;
  constexpr bool findLastBlock = true;
  std::optional<uint64_t> begin = firstFrom;
  if (before.has_value()) begin = blockWith(source, channel, grid, *firstFrom, firstRangeEnd, !*before, findFirstBlock);
  if (!begin.has_value()) return std::nullopt;
  std::optional<uint64_t> endBlock = lastTo;
  if (after.has_value()) endBlock = blockWith(source, channel, grid, *lastFrom, lastRangeEnd, !*after, findLastBlock);
  if (!endBlock.has_value()) return std::nullopt;
  const uint64_t end = (std::min)(*endBlock + grid.samples, total);
  if (end <= *begin) return std::nullopt;

  Candidate out;
  out.beginSample = *begin;
  out.endSample = end;
  out.levelBefore = before.value_or(after.value_or(false));
  out.levelAfter = after.value_or(out.levelBefore);
  out.levelsKnown = true;
  out.openBefore = open.before;
  out.openAfter = open.after;
  return out;
}

}

double DenseSpanPaint::ink() const {
  double total = 0.0;
  for (size_t i = 0; i < count; ++i) {
    total += static_cast<double>(parts[i].lastColumn - parts[i].firstColumn + 1) * parts[i].coverage;
  }
  return total;
}

namespace {

struct RowScan {
  uint32_t width = 0;
  double perColumn = 0.0;
  uint64_t total = 0;
  uint64_t gapMin = 0;
  BlockGrid grid;
  bool fromBlocks = false;
};

std::optional<RowScan> rowScanOf(const WaveformSource& source, uint32_t channel, const Viewport& viewport,
                                 std::span<const uint8_t> columns) {
  RowScan scan;
  scan.width = static_cast<uint32_t>(columns.size());
  scan.perColumn = viewport.samplesPerPixel();
  if (scan.width < 3 || scan.perColumn < 2.0 || channel >= source.channelCount()) return std::nullopt;
  scan.total = source.sampleCount();
  if (scan.total == 0) return std::nullopt;
  const auto maxColumnBoundarySpacingSamples = static_cast<uint64_t>(std::ceil(scan.perColumn));
  scan.gapMin = kDenseGapMinColumns * maxColumnBoundarySpacingSamples;
  scan.grid = BlockGrid::of(source);
  scan.fromBlocks = scan.grid.samples != 0 &&
                    static_cast<double>(scan.grid.samples * kDenseBlockEndMinBlocks) <= scan.perColumn;
  return scan;
}

bool isFlat(uint8_t column) {
  return column == static_cast<uint8_t>(ColumnLevel::Low) || column == static_cast<uint8_t>(ColumnLevel::High);
}

Candidate boundaryEdge(const RowScan& scan, uint64_t edge, uint32_t column, bool before, bool after) {
  Candidate out{edge, edge, column, column, before, after, true, false, false};
  if (!scan.fromBlocks) return out;
  const std::optional<uint64_t> holding = scan.grid.blockHolding(edge);
  const std::optional<uint64_t> previous = scan.grid.blockHolding(edge - 1);
  if (!holding.has_value() || !previous.has_value()) return out;
  out.beginSample = *holding;
  out.endSample = (std::min)(*previous + scan.grid.samples, scan.total);
  return out;
}

void probeRuns(const WaveformSource& source, uint32_t channel, const Viewport& viewport, const RowScan& scan,
               std::span<const uint8_t> columns, uint32_t firstChangedColumn, std::vector<Candidate>& candidates) {
  const uint32_t width = scan.width;
  const uint64_t total = scan.total;
  const BlockGrid& grid = scan.grid;
  const auto transition = static_cast<uint8_t>(ColumnLevel::Transition);
  const auto sampleAt = [&](uint32_t column) {
    return static_cast<int64_t>(std::floor(viewport.pixelToSample(static_cast<double>(column))));
  };
  for (uint32_t j = 0; j < width;) {
    if (columns[j] != transition) {
      // WHY: Flat neighbours of two levels hide an edge on their boundary; skipped, regions split with scroll.
      if (j > 0 && j + 1 >= firstChangedColumn && isFlat(columns[j]) && isFlat(columns[j - 1]) &&
          columns[j] != columns[j - 1]) {
        const int64_t edge = sampleAt(j);
        if (edge > 0 && static_cast<uint64_t>(edge) < total) {
          const auto high = static_cast<uint8_t>(ColumnLevel::High);
          candidates.push_back(
            boundaryEdge(scan, static_cast<uint64_t>(edge), j, columns[j - 1] == high, columns[j] == high));
        }
      }
      ++j;
      continue;
    }
    const uint32_t first = j;
    uint32_t last = j;
    while (last + 1 < width && columns[last + 1] == transition) ++last;
    j = last + 1;
    // WHY: A run whose NEXT column precedes the first changed one probes as before, so its kept probe stands.
    if (last + 1 < firstChangedColumn) continue;
    const int64_t rawBegin = (std::max)(sampleAt(first), int64_t{0});
    const int64_t rawEnd = sampleAt(last + 1);
    if (rawEnd <= rawBegin) continue;

    // WHY: Every run is probed, even one column: skipping lone runs drew one column at some scroll offsets and three
    // at others.
    const auto runBegin = static_cast<uint64_t>(rawBegin);
    const uint64_t runEnd = (std::min)(static_cast<uint64_t>(rawEnd), total);
    if (runEnd <= runBegin + 1) continue;
    const OpenSides open{first == 0 && runBegin > 0, last + 1 >= width && runEnd < total};

    if (scan.fromBlocks) {
      const int64_t firstEnd = sampleAt(first + 1);
      const int64_t lastBegin = sampleAt(last);
      if (firstEnd > rawBegin && lastBegin >= rawBegin) {
        std::optional<Candidate> found = blockCandidate(
          source, channel, grid, runBegin, (std::min)(static_cast<uint64_t>(firstEnd), total),
          static_cast<uint64_t>(lastBegin), runEnd, total, open);
        if (found.has_value()) {
          found->firstColumn = first;
          found->lastColumn = last;
          candidates.push_back(*found);
          continue;
        }
      }
    }

    // WHY: Last probe asks at runEnd, not runEnd - 1: an edge at i makes column i - 1 a transition, else the region
    // moves with the grid.
    const std::optional<uint64_t> firstEdge = source.nextEdge(channel, runBegin == 0 ? 0 : runBegin - 1);
    if (!firstEdge.has_value() || *firstEdge >= runEnd) continue;
    const std::optional<uint64_t> lastEdge = source.previousEdge(channel, runEnd);
    if (!lastEdge.has_value() || *lastEdge < *firstEdge) continue;
    candidates.push_back(Candidate{*firstEdge, *lastEdge, first, last, false, false, false, open.before, open.after});
  }
}

void spansOfCandidates(const WaveformSource& source, uint32_t channel, const Viewport& viewport, const RowScan& scan,
                       std::span<const Candidate> candidates, size_t firstCandidate, std::vector<DenseSpan>& out,
                       std::vector<size_t>& spanFirstProbe) {
  struct Region {
    Candidate merged;
    size_t first = 0;
  };
  std::vector<Region> regions;
  for (size_t i = firstCandidate; i < candidates.size(); ++i) {
    const Candidate& candidate = candidates[i];
    if (!regions.empty() && candidate.beginSample - regions.back().merged.endSample < scan.gapMin) {
      Candidate& back = regions.back().merged;
      back.endSample = candidate.endSample;
      back.lastColumn = candidate.lastColumn;
      back.levelAfter = candidate.levelAfter;
      back.levelsKnown = back.levelsKnown && candidate.levelsKnown;
      back.openAfter = candidate.openAfter;
      continue;
    }
    regions.push_back(Region{candidate, i});
  }

  for (const Region& r : regions) {
    const Candidate& region = r.merged;
    if (region.endSample <= region.beginSample) continue;
    DenseSpan span;
    span.beginSample = region.beginSample;
    span.endSample = region.endSample;
    span.beginPixel = viewport.sampleToPixel(static_cast<double>(region.beginSample));
    span.endPixel = viewport.sampleToPixel(static_cast<double>(region.endSample));
    span.samplesPerColumn = scan.perColumn;
    span.firstColumn = region.firstColumn;
    span.lastColumn = region.lastColumn;
    span.openBefore = region.openBefore;
    span.openAfter = region.openAfter;
    if (region.levelsKnown) {
      span.levelBefore = region.levelBefore;
      span.levelAfter = region.levelAfter;
    } else {
      span.levelBefore = source.level(channel, region.beginSample - 1);
      span.levelAfter = source.level(channel, region.endSample);
    }
    out.push_back(span);
    spanFirstProbe.push_back(r.first);
  }
}

}

std::vector<DenseSpan> denseSpansOf(const WaveformSource& source, uint32_t channel,
                                    const Viewport& viewport, std::span<const uint8_t> columns,
                                    DenseProbes* probes) {
  std::vector<DenseSpan> out;
  DenseProbes found;
  if (const std::optional<RowScan> scan = rowScanOf(source, channel, viewport, columns)) {
    constexpr uint32_t kEveryRun = 0;
    probeRuns(source, channel, viewport, *scan, columns, kEveryRun, found.probes);
    spansOfCandidates(source, channel, viewport, *scan, found.probes, 0, out, found.spanFirstProbe);
  }
  if (probes != nullptr) *probes = std::move(found);
  return out;
}

void extendDenseSpans(const WaveformSource& source, uint32_t channel, const Viewport& viewport,
                      std::span<const uint8_t> columns, uint32_t firstChangedColumn, std::vector<DenseSpan>& spans,
                      DenseProbes& probes) {
  const std::optional<RowScan> scan = rowScanOf(source, channel, viewport, columns);
  if (!scan.has_value()) {
    spans.clear();
    probes = DenseProbes{};
    return;
  }
  size_t kept = 0;
  while (kept < probes.probes.size() && probes.probes[kept].lastColumn + 1 < firstChangedColumn) ++kept;
  DenseProbes grown;
  grown.probes.assign(probes.probes.begin(), probes.probes.begin() + static_cast<std::ptrdiff_t>(kept));
  probeRuns(source, channel, viewport, *scan, columns, firstChangedColumn, grown.probes);

  size_t keptSpans = 0;
  while (keptSpans < probes.spanFirstProbe.size() && probes.spanFirstProbe[keptSpans] < kept) ++keptSpans;
  size_t rebuildFrom = 0;
  // WHY: Last span begun by kept probes is rebuilt: new probes merge into it or a spanless region after it.
  if (keptSpans > 0) rebuildFrom = probes.spanFirstProbe[--keptSpans];
  std::vector<DenseSpan> out(spans.begin(), spans.begin() + static_cast<std::ptrdiff_t>(keptSpans));
  grown.spanFirstProbe.assign(probes.spanFirstProbe.begin(),
                              probes.spanFirstProbe.begin() + static_cast<std::ptrdiff_t>(keptSpans));
  spansOfCandidates(source, channel, viewport, *scan, grown.probes, rebuildFrom, out, grown.spanFirstProbe);
  spans = std::move(out);
  probes = std::move(grown);
}

void flattenDenseColumns(std::span<uint8_t> columns, std::span<const DenseSpan> spans) {
  const auto width = static_cast<uint32_t>(columns.size());
  for (const DenseSpan& span : spans) {
    const DenseSpanPaint paint = denseSpanPaint(span, width);
    if (paint.count == 0) continue;
    const uint32_t barFirst = paint.parts[0].firstColumn;
    const uint32_t barLast = paint.parts[paint.count - 1].lastColumn;
    // WHY: The level change goes UNDER the bar: a vertical drawn beside it would move with the column grid.
    const double middle = (span.beginPixel + span.endPixel) * 0.5;
    const auto raw = static_cast<int64_t>(std::ceil(middle - 0.5));
    const auto change = static_cast<uint32_t>((std::max)(
      static_cast<int64_t>(barFirst), (std::min)(raw, static_cast<int64_t>(barLast))));
    const uint32_t from = (std::min)(span.firstColumn, barFirst);
    const uint32_t to = (std::max)(span.lastColumn, barLast);
    for (uint32_t j = from; j <= to && j < width; ++j) {
      const bool level = j < change ? span.levelBefore : span.levelAfter;
      columns[j] = static_cast<uint8_t>(level ? ColumnLevel::High : ColumnLevel::Low);
    }
  }
}

uint32_t denseSpanWidthColumns(uint64_t lengthSamples, double samplesPerColumn) {
  if (lengthSamples == 0 || !(samplesPerColumn > 0.0)) return 1;
  const double columns = std::ceil(static_cast<double>(lengthSamples) / samplesPerColumn);
  if (!(columns > 1.0)) return 1;
  return static_cast<uint32_t>(columns);
}

DenseSpanPaint denseSpanPaint(const DenseSpan& span, uint32_t widthColumns) {
  DenseSpanPaint paint;
  if (widthColumns == 0 || span.endSample <= span.beginSample) return paint;

  if (span.openBefore || span.openAfter) {
    const uint32_t last = (std::min)(span.lastColumn, widthColumns - 1);
    if (last < span.firstColumn) return paint;
    paint.parts[0] = DenseSpanInk{span.firstColumn, last, 1.0};
    paint.count = 1;
    return paint;
  }

  const auto columns =
    static_cast<int64_t>(denseSpanWidthColumns(span.endSample - span.beginSample, span.samplesPerColumn));

  const double centre = (span.beginPixel + span.endPixel) * 0.5;
  auto first = static_cast<int64_t>(std::floor(centre - static_cast<double>(columns) * 0.5 + 0.5));

  // WHY: Bar width is never shrunk to fit its region: that would put the column grid back into the width.
  const auto runFirst = static_cast<int64_t>(span.firstColumn);
  const auto runLast = static_cast<int64_t>(span.lastColumn);
  if (columns <= runLast - runFirst + 1) {
    first = (std::max)(runFirst, (std::min)(first, runLast - columns + 1));
  }

  const auto view = static_cast<int64_t>(widthColumns);
  first = (std::max)(int64_t{0}, (std::min)(first, view - columns));
  first = (std::max)(int64_t{0}, first);
  const int64_t last = (std::min)(first + columns - 1, view - 1);
  if (last < first) return paint;
  paint.parts[0] =
    DenseSpanInk{static_cast<uint32_t>(first), static_cast<uint32_t>(last), 1.0};
  paint.count = 1;
  return paint;
}

}
