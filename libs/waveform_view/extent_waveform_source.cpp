/*
 * SPDX-License-Identifier: MIT
 */

#include "extent_waveform_source.h"

#include <algorithm>

namespace bitoscope::waveform_view {

ExtentWaveformSource::ExtentWaveformSource(const WaveformSource& inner, uint64_t first, uint64_t count)
    : inner_(inner), first_(first), count_(count) {}

void ExtentWaveformSource::setExtent(uint64_t first, uint64_t count) {
  first_ = first;
  count_ = count;
}

uint32_t ExtentWaveformSource::channelCount() const { return inner_.channelCount(); }

uint64_t ExtentWaveformSource::sampleCount() const {
  const uint64_t held = inner_.sampleCount();
  if (held <= first_) return 0;
  return (std::min)(count_, held - first_);
}

uint64_t ExtentWaveformSource::sampleRateMilliHz() const { return inner_.sampleRateMilliHz(); }

std::expected<void, std::string> ExtentWaveformSource::summary(uint32_t channel, uint64_t begin, uint64_t end,
                                                               std::span<ColumnSummary> out) const {
  if (begin >= end || end > sampleCount()) {
    return std::unexpected("range [" + std::to_string(begin) + ", " + std::to_string(end) +
                           ") is not inside capture's " + std::to_string(sampleCount()) + " samples");
  }
  return inner_.summary(channel, first_ + begin, first_ + end, out);
}

uint64_t ExtentWaveformSource::summaryBlockSamples() const { return inner_.summaryBlockSamples(); }

uint64_t ExtentWaveformSource::summaryBlockPhase() const {
  const uint64_t block = inner_.summaryBlockSamples();
  if (block == 0) return 0;
  return (inner_.summaryBlockPhase() + first_) % block;
}

uint64_t ExtentWaveformSource::summaryPendingBefore() const {
  const uint64_t inner = inner_.summaryPendingBefore();
  return inner > first_ ? inner - first_ : 0;
}

std::optional<ColumnSummary> ExtentWaveformSource::blockRange(uint32_t channel, uint64_t begin,
                                                              uint64_t end) const {
  if (summaryBlockSamples() == 0) return std::nullopt;
  if (begin >= end || end > sampleCount()) return std::nullopt;
  return inner_.blockRange(channel, first_ + begin, first_ + end);
}

bool ExtentWaveformSource::level(uint32_t channel, uint64_t sample) const {
  return inner_.level(channel, first_ + sample);
}

std::optional<uint64_t> ExtentWaveformSource::nextEdge(uint32_t channel, uint64_t from) const {
  const std::optional<uint64_t> edge = inner_.nextEdge(channel, first_ + from);
  if (!edge || *edge - first_ >= sampleCount()) return std::nullopt;
  return *edge - first_;
}

std::optional<uint64_t> ExtentWaveformSource::previousEdge(uint32_t channel, uint64_t from) const {
  if (sampleCount() == 0) return std::nullopt;
  const uint64_t last = (std::min)(from, sampleCount() - 1);
  const std::optional<uint64_t> edge = inner_.previousEdge(channel, first_ + last);
  if (!edge || *edge <= first_) return std::nullopt;
  return *edge - first_;
}

EmptyWaveformSource::EmptyWaveformSource(uint32_t channels, uint64_t sampleRateMilliHz)
    : channels_(channels), rate_(sampleRateMilliHz) {}

std::expected<void, std::string> EmptyWaveformSource::summary(uint32_t, uint64_t, uint64_t,
                                                              std::span<ColumnSummary>) const {
  return std::unexpected(std::string("there are no samples"));
}

}
