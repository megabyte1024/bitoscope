/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>

namespace bitoscope::waveform_view {

struct ColumnSummary {
  bool has_zero = false;
  bool has_one = false;
};

class WaveformSource {
 public:
  virtual ~WaveformSource() = default;

  virtual uint32_t channelCount() const = 0;

  virtual uint64_t sampleCount() const = 0;

  virtual uint64_t sampleRateMilliHz() const = 0;

  virtual std::expected<void, std::string> summary(uint32_t channel, uint64_t begin, uint64_t end,
                                                     std::span<ColumnSummary> out) const = 0;

  virtual uint64_t summaryBlockSamples() const { return 0; }

  virtual uint64_t summaryBlockPhase() const { return 0; }

  virtual uint64_t summaryPendingBefore() const { return 0; }

  virtual std::optional<ColumnSummary> blockRange([[maybe_unused]] uint32_t channel,
                                                  [[maybe_unused]] uint64_t begin,
                                                  [[maybe_unused]] uint64_t end) const {
    return std::nullopt;
  }

  virtual bool level(uint32_t channel, uint64_t sample) const = 0;

  virtual std::optional<uint64_t> nextEdge(uint32_t channel, uint64_t from) const = 0;

  virtual std::optional<uint64_t> previousEdge(uint32_t channel, uint64_t from) const = 0;
};

}
