/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace bitoscope::waveform_view {

class Viewport;
class WaveformSource;

// WHY: Gap of two columns: 2 x ceil(samplesPerPixel) always holds a whole drawn column at every scroll offset,
// one column does not.
inline constexpr uint64_t kDenseGapMinColumns = 2;

// WHY: Ends come from blocks at four or more per column: sample probes there decode extra segments and cost 33%
// per frame.
inline constexpr uint64_t kDenseBlockEndMinBlocks = 4;

struct DenseSpan {
  uint64_t beginSample = 0;
  uint64_t endSample = 0;
  double beginPixel = 0.0;
  double endPixel = 0.0;
  uint32_t firstColumn = 0;
  uint32_t lastColumn = 0;
  bool levelBefore = false;
  bool levelAfter = false;

  double samplesPerColumn = 1.0;

  bool operator==(const DenseSpan&) const = default;
};

struct DenseProbe {
  uint64_t beginSample = 0;
  uint64_t endSample = 0;
  uint32_t firstColumn = 0;
  uint32_t lastColumn = 0;
  bool levelBefore = false;
  bool levelAfter = false;
  bool levelsKnown = false;

  bool operator==(const DenseProbe&) const = default;
};

struct DenseProbes {
  std::vector<DenseProbe> probes;
  std::vector<size_t> spanFirstProbe;
};

std::vector<DenseSpan> denseSpansOf(const WaveformSource& source, uint32_t channel,
                                    const Viewport& viewport, std::span<const uint8_t> columns,
                                    DenseProbes* probes = nullptr);

void extendDenseSpans(const WaveformSource& source, uint32_t channel, const Viewport& viewport,
                      std::span<const uint8_t> columns, uint32_t firstChangedColumn, std::vector<DenseSpan>& spans,
                      DenseProbes& probes);

void flattenDenseColumns(std::span<uint8_t> columns, std::span<const DenseSpan> spans);

struct DenseSpanInk {
  uint32_t firstColumn = 0;
  uint32_t lastColumn = 0;
  double coverage = 0.0;
};

struct DenseSpanPaint {
  std::array<DenseSpanInk, 3> parts{};
  size_t count = 0;

  double ink() const;
};

uint32_t denseSpanWidthColumns(uint64_t lengthSamples, double samplesPerColumn);

DenseSpanPaint denseSpanPaint(const DenseSpan& span, uint32_t widthColumns);

}
