/*
 * SPDX-License-Identifier: MIT
 */

#include "channel_render.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <utility>

#include "viewport.h"

namespace bitoscope::waveform_view {

BlockGrid BlockGrid::of(const WaveformSource& source) {
  BlockGrid grid;
  grid.samples = source.summaryBlockSamples();
  if (grid.samples != 0) grid.phase = source.summaryBlockPhase() % grid.samples;
  return grid;
}

bool BlockGrid::aligned(uint64_t at) const { return samples != 0 && (at + phase) % samples == 0; }

uint64_t BlockGrid::firstBlockFrom(uint64_t at) const {
  if (samples == 0) return at;
  // WHY: Round with the phase added, then subtract it: the result lands on a real block boundary and unsigned math
  // never wraps.
  return (at + phase + samples - 1) / samples * samples - phase;
}

uint64_t BlockGrid::lastBlockEndBy(uint64_t at) const {
  if (samples == 0) return 0;
  const uint64_t boundary = (at + phase) / samples * samples;
  // WHY: boundary below phase means at lies in the block straddling sample 0: no whole block ends before it, and 0
  // says so.
  return boundary < phase ? 0 : boundary - phase;
}

std::optional<uint64_t> BlockGrid::blockHolding(uint64_t at) const {
  if (samples == 0) return std::nullopt;
  const uint64_t into = (at + phase) % samples;
  if (into > at) return std::nullopt;
  return at - into;
}

namespace {

// WHY: Partly covered blocks are used only when flat: summary() ORs touching blocks, a superset that invents
// transitions.
std::optional<ColumnLevel> blockColumn(const WaveformSource& source, uint32_t channel,
                                        const BlockGrid& grid, uint64_t begin, uint64_t end) {
  bool hasZero = false;
  bool hasOne = false;
  const auto take = [&](const ColumnSummary& s) {
    hasZero = hasZero || s.has_zero;
    hasOne = hasOne || s.has_one;
  };
  const uint64_t firstWhole = grid.firstBlockFrom(begin);
  const uint64_t pastWhole = grid.lastBlockEndBy(end);
  if (firstWhole < pastWhole) {
    const auto whole = source.blockRange(channel, firstWhole, pastWhole);
    if (!whole.has_value()) return std::nullopt;
    take(*whole);
    if (hasZero && hasOne) return ColumnLevel::Transition;
  }
  std::optional<uint64_t> partial[2];
  size_t partials = 0;
  if (!grid.aligned(begin)) partial[partials++] = grid.blockHolding(begin);
  if (!grid.aligned(end)) {
    const std::optional<uint64_t> at = grid.blockHolding(end);
    if (partials == 0 || partial[0] != at) partial[partials++] = at;
  }
  for (size_t i = 0; i < partials; ++i) {
    if (!partial[i].has_value()) return std::nullopt;
    const auto one = source.blockRange(channel, *partial[i], *partial[i] + grid.samples);
    if (!one.has_value()) return std::nullopt;
    if (one->has_zero && one->has_one) return std::nullopt;
    take(*one);
    if (hasZero && hasOne) return ColumnLevel::Transition;
  }
  if (hasZero == hasOne) return std::nullopt;
  return hasOne ? ColumnLevel::High : ColumnLevel::Low;
}

// WHY: grid is read once per render: asking the source per column costs two virtual calls on every column.
ColumnLevel columnLevel(const WaveformSource& source, uint32_t channel, uint64_t total,
                        const BlockGrid& grid, uint64_t summaryPendingBefore, uint64_t begin, uint64_t end) {
  if (begin >= total) return ColumnLevel::NoData;
  if (end > total) end = total;
  if (end <= begin) end = begin + 1;
  if (end > total) return ColumnLevel::NoData;
  if (grid.samples != 0 && end - begin >= grid.samples * kBlockColumnMinBlocks) {
    if (const auto level = blockColumn(source, channel, grid, begin, end)) return *level;
    // WHY: Left empty, not read sample by sample: that read stalled frames while a trigger's history was summarized.
    if (begin < summaryPendingBefore) return ColumnLevel::NoData;
  }
  const bool startLevel = source.level(channel, begin);
  if (end - begin > 1) {
    const auto edge = source.nextEdge(channel, begin);
    if (edge.has_value() && *edge < end) return ColumnLevel::Transition;
  }
  return startLevel ? ColumnLevel::High : ColumnLevel::Low;
}

std::pair<uint64_t, uint64_t> clampedColumn(int64_t begin, int64_t end) {
  if (end <= begin) end = begin + 1;
  const uint64_t b = begin < 0 ? 0 : static_cast<uint64_t>(begin);
  const uint64_t e = end < 0 ? 0 : static_cast<uint64_t>(end);
  return {b, e};
}

int64_t columnBeginSample(const Viewport& viewport, uint32_t column) {
  return static_cast<int64_t>(std::floor(viewport.pixelToSample(static_cast<double>(column))));
}

}

std::expected<void, std::string> renderChannelColumns(const WaveformSource& source, uint32_t channel,
                                                      const Viewport& viewport, uint32_t firstColumn,
                                                      std::span<uint8_t> columns) {
  const uint32_t width = (std::min)(viewport.widthPixels(), static_cast<uint32_t>(columns.size()));
  if (firstColumn >= width) return {};
  if (channel >= source.channelCount()) {
    return std::unexpected("channel " + std::to_string(channel) + " does not exist");
  }

  const uint64_t total = source.sampleCount();
  const BlockGrid grid = BlockGrid::of(source);
  const uint64_t summaryPendingBefore = source.summaryPendingBefore();

  int64_t begin = columnBeginSample(viewport, firstColumn);
  for (uint32_t j = firstColumn; j < width; ++j) {
    const int64_t next = columnBeginSample(viewport, j + 1);
    // WHY: A column wholly before sample 0 has no data: clampedColumn() alone would turn it into a read of sample 0.
    if ((std::max)(next, begin + 1) <= 0) {
      columns[j] = static_cast<uint8_t>(ColumnLevel::NoData);
    } else {
      const auto [colB, colE] = clampedColumn(begin, next);
      if (colB >= total) {
        std::fill(columns.begin() + j, columns.begin() + width, static_cast<uint8_t>(ColumnLevel::NoData));
        break;
      }
      columns[j] =
        static_cast<uint8_t>(columnLevel(source, channel, total, grid, summaryPendingBefore, colB, colE));
    }
    begin = next;
  }
  return {};
}

uint32_t firstColumnReachingPast(const Viewport& viewport, uint64_t samples) {
  const auto reachesPast = [&](uint32_t j) {
    const int64_t end = (std::max)(columnBeginSample(viewport, j + 1), columnBeginSample(viewport, j) + 1);
    return end > 0 && static_cast<uint64_t>(end) > samples;
  };
  uint32_t low = 0;
  uint32_t high = viewport.widthPixels();
  while (low < high) {
    const uint32_t middle = low + (high - low) / 2;
    if (reachesPast(middle)) {
      high = middle;
    } else {
      low = middle + 1;
    }
  }
  return low;
}

std::vector<double> samplePointsOf(const Viewport& viewport, uint64_t total) {
  std::vector<double> out;
  const uint32_t width = viewport.widthPixels();
  if (width == 0 || viewport.samplesPerPixel() > kSampleDotMaxSamplesPerPixel) return out;
  const auto [visB, visE] = clampedColumn(columnBeginSample(viewport, 0), columnBeginSample(viewport, width));
  const uint64_t dotsEnd = (std::min)(visE, total);
  // WHY: Dot sits at the centre of the first column showing its sample, as samplePixelX() does, so it lies on its
  // transition line.
  for (uint64_t s = visB; s < dotsEnd; ++s) {
    const double x = viewport.samplePixelX(static_cast<int64_t>(s));
    if (x >= static_cast<double>(width)) break;
    out.push_back(x);
  }
  return out;
}

std::expected<ChannelRender, std::string> renderChannel(const WaveformSource& source, uint32_t channel,
                                                          const Viewport& viewport) {
  const uint32_t width = viewport.widthPixels();
  ChannelRender result;
  result.columns.assign(width, static_cast<uint8_t>(ColumnLevel::NoData));
  if (width == 0) return result;
  if (auto drawn = renderChannelColumns(source, channel, viewport, 0, result.columns); !drawn) {
    return std::unexpected(std::move(drawn.error()));
  }
  result.samplePointsX = samplePointsOf(viewport, source.sampleCount());
  return result;
}

}
