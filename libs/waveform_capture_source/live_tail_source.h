/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>

#include "capture_snapshot.h"
#include "waveform_source.h"

namespace bitoscope::waveform_capture_source {

class LiveTailWaveformSource final : public waveform_view::WaveformSource {
 public:
  LiveTailWaveformSource(const waveform_view::WaveformSource& stored, const capture::CaptureTail& tail);

  uint32_t channelCount() const override;
  uint64_t sampleCount() const override;
  uint64_t sampleRateMilliHz() const override;
  std::expected<void, std::string> summary(uint32_t channel, uint64_t begin, uint64_t end,
                                           std::span<waveform_view::ColumnSummary> out) const override;
  uint64_t summaryBlockSamples() const override;
  uint64_t summaryBlockPhase() const override;
  uint64_t summaryPendingBefore() const override;
  std::optional<waveform_view::ColumnSummary> blockRange(uint32_t channel, uint64_t begin,
                                                         uint64_t end) const override;
  bool level(uint32_t channel, uint64_t sample) const override;
  std::optional<uint64_t> nextEdge(uint32_t channel, uint64_t from) const override;
  std::optional<uint64_t> previousEdge(uint32_t channel, uint64_t from) const override;

 private:
  bool reachesTail(uint64_t end) const;
  uint64_t tailEnd() const;
  std::optional<uint64_t> nextInTail(uint32_t channel, bool level, uint64_t begin) const;
  std::optional<uint64_t> lastInTail(uint32_t channel, bool level, uint64_t end) const;
  waveform_view::ColumnSummary exactRange(uint32_t channel, uint64_t begin, uint64_t end) const;

  const waveform_view::WaveformSource& stored_;
  const capture::CaptureTail& tail_;
};

}
