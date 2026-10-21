/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "waveform_source.h"

namespace bitoscope::waveform_view {

class Viewport;

enum class ColumnLevel : uint8_t { Low = 0, High = 1, Transition = 2, NoData = 3 };

constexpr double kSampleDotMaxSamplesPerPixel = 1.0 / 8.0;

// WHY: Eight blocks: measured break-even is about six blocks per column, and eight is the first power of two
// past it.
constexpr uint64_t kBlockColumnMinBlocks = 8;

// WHY: Block boundaries come from BlockGrid, phase included: multiples of the block size would name ranges the
// pyramid refuses.
struct BlockGrid {
  uint64_t samples = 0;
  uint64_t phase = 0;

  static BlockGrid of(const WaveformSource& source);

  bool aligned(uint64_t at) const;

  uint64_t firstBlockFrom(uint64_t at) const;

  uint64_t lastBlockEndBy(uint64_t at) const;

  std::optional<uint64_t> blockHolding(uint64_t at) const;
};

struct ChannelRender {
  std::vector<uint8_t> columns;
  std::vector<double> samplePointsX;
};

// WHY: Source summary() is deliberately unused: it ORs blocks touching a column, so the picture would change
// with view position.
std::expected<ChannelRender, std::string> renderChannel(const WaveformSource& source, uint32_t channel,
                                                          const Viewport& viewport);

std::expected<void, std::string> renderChannelColumns(const WaveformSource& source, uint32_t channel,
                                                      const Viewport& viewport, uint32_t firstColumn,
                                                      std::span<uint8_t> columns);

uint32_t firstColumnReachingPast(const Viewport& viewport, uint64_t samples);

std::vector<double> samplePointsOf(const Viewport& viewport, uint64_t total);

}
