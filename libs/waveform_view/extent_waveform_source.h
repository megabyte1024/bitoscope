/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>

#include "waveform_source.h"

namespace bitoscope::waveform_view {

class ExtentWaveformSource final : public WaveformSource {
 public:
  ExtentWaveformSource(const WaveformSource& inner, uint64_t first, uint64_t count);

  void setExtent(uint64_t first, uint64_t count);

  uint32_t channelCount() const override;
  uint64_t sampleCount() const override;
  uint64_t sampleRateMilliHz() const override;
  std::expected<void, std::string> summary(uint32_t channel, uint64_t begin, uint64_t end,
                                           std::span<ColumnSummary> out) const override;
  uint64_t summaryBlockSamples() const override;
  uint64_t summaryBlockPhase() const override;
  uint64_t summaryPendingBefore() const override;
  std::optional<ColumnSummary> blockRange(uint32_t channel, uint64_t begin, uint64_t end) const override;
  bool level(uint32_t channel, uint64_t sample) const override;
  std::optional<uint64_t> nextEdge(uint32_t channel, uint64_t from) const override;
  std::optional<uint64_t> previousEdge(uint32_t channel, uint64_t from) const override;

 private:
  const WaveformSource& inner_;
  uint64_t first_ = 0;
  uint64_t count_ = 0;
};

class EmptyWaveformSource final : public WaveformSource {
 public:
  EmptyWaveformSource(uint32_t channels, uint64_t sampleRateMilliHz);

  uint32_t channelCount() const override { return channels_; }
  uint64_t sampleCount() const override { return 0; }
  uint64_t sampleRateMilliHz() const override { return rate_; }
  std::expected<void, std::string> summary(uint32_t channel, uint64_t begin, uint64_t end,
                                           std::span<ColumnSummary> out) const override;
  bool level(uint32_t, uint64_t) const override { return false; }
  std::optional<uint64_t> nextEdge(uint32_t, uint64_t) const override { return std::nullopt; }
  std::optional<uint64_t> previousEdge(uint32_t, uint64_t) const override { return std::nullopt; }

 private:
  uint32_t channels_;
  uint64_t rate_;
};

}
