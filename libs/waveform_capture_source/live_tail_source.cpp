/*
 * SPDX-License-Identifier: MIT
 */

#include "live_tail_source.h"

#include <algorithm>
#include <bit>

namespace bitoscope::waveform_capture_source {

namespace wv = bitoscope::waveform_view;

namespace {

constexpr uint64_t kWordSamples = 64;

}

LiveTailWaveformSource::LiveTailWaveformSource(const wv::WaveformSource& stored, const capture::CaptureTail& tail)
    : stored_(stored), tail_(tail) {}

uint32_t LiveTailWaveformSource::channelCount() const { return stored_.channelCount(); }

uint64_t LiveTailWaveformSource::sampleCount() const {
  return tail_.count == 0 ? stored_.sampleCount() : tailEnd();
}

uint64_t LiveTailWaveformSource::sampleRateMilliHz() const { return stored_.sampleRateMilliHz(); }

std::expected<void, std::string> LiveTailWaveformSource::summary(uint32_t channel, uint64_t begin, uint64_t end,
                                                                 std::span<wv::ColumnSummary> out) const {
  if (!reachesTail(end)) return stored_.summary(channel, begin, end, out);
  if (channel >= channelCount()) return std::unexpected("channel " + std::to_string(channel) + " does not exist");
  const uint64_t block = summaryBlockSamples();
  if (block == 0) return std::unexpected(std::string("this capture builds no pyramid"));
  if (out.empty()) return std::unexpected(std::string("summary needs at least one column"));
  if (begin >= end) {
    return std::unexpected("range [" + std::to_string(begin) + ", " + std::to_string(end) + ") is empty");
  }
  if (end > tailEnd()) {
    return std::unexpected("range ends at " + std::to_string(end) + " but only " + std::to_string(tailEnd()) +
                           " samples have been captured");
  }
  const uint64_t range = end - begin;
  const uint64_t columns = out.size();
  if (range / columns < block) {
    return std::unexpected("columns of " + std::to_string(range / columns) +
                           " samples are narrower than pyramid's base bucket: read segments directly");
  }
  for (uint64_t j = 0; j < columns; ++j) {
    const uint64_t s = begin + range / columns * j + range % columns * j / columns;
    const uint64_t e = begin + range / columns * (j + 1) + range % columns * (j + 1) / columns;
    out[static_cast<size_t>(j)] = exactRange(channel, s, e);
  }
  return {};
}

uint64_t LiveTailWaveformSource::summaryBlockSamples() const { return stored_.summaryBlockSamples(); }

uint64_t LiveTailWaveformSource::summaryBlockPhase() const { return stored_.summaryBlockPhase(); }

uint64_t LiveTailWaveformSource::summaryPendingBefore() const { return stored_.summaryPendingBefore(); }

std::optional<wv::ColumnSummary> LiveTailWaveformSource::blockRange(uint32_t channel, uint64_t begin,
                                                                    uint64_t end) const {
  if (!reachesTail(end)) return stored_.blockRange(channel, begin, end);
  const uint64_t block = summaryBlockSamples();
  if (channel >= channelCount() || block == 0 || begin >= end || end > tailEnd()) return std::nullopt;
  if (begin % block != 0 || end % block != 0) return std::nullopt;
  return exactRange(channel, begin, end);
}

bool LiveTailWaveformSource::level(uint32_t channel, uint64_t sample) const {
  if (!reachesTail(sample + 1)) return stored_.level(channel, sample);
  return tail_.level(channel, sample);
}

std::optional<uint64_t> LiveTailWaveformSource::nextEdge(uint32_t channel, uint64_t from) const {
  if (tail_.count == 0) return stored_.nextEdge(channel, from);
  if (from >= tail_.first_sample) return nextInTail(channel, tail_.level(channel, from), from + 1);
  if (const auto edge = stored_.nextEdge(channel, from); edge && *edge < tail_.first_sample) return edge;
  return nextInTail(channel, stored_.level(channel, from), tail_.first_sample);
}

std::optional<uint64_t> LiveTailWaveformSource::previousEdge(uint32_t channel, uint64_t from) const {
  if (tail_.count == 0) return stored_.previousEdge(channel, from);
  from = (std::min)(from, tailEnd() - 1);
  if (from < tail_.first_sample) return stored_.previousEdge(channel, from);
  const bool current = tail_.level(channel, from);
  if (const auto other = lastInTail(channel, current, from)) return *other + 1;
  if (tail_.first_sample == 0) return std::nullopt;
  if (stored_.level(channel, tail_.first_sample - 1) != current) return tail_.first_sample;
  return stored_.previousEdge(channel, tail_.first_sample - 1);
}

bool LiveTailWaveformSource::reachesTail(uint64_t end) const { return tail_.count != 0 && end > tail_.first_sample; }

uint64_t LiveTailWaveformSource::tailEnd() const { return tail_.first_sample + tail_.count; }

std::optional<uint64_t> LiveTailWaveformSource::nextInTail(uint32_t channel, bool level, uint64_t begin) const {
  const uint64_t stop = tail_.count;
  const uint64_t at = begin - tail_.first_sample;
  const uint64_t flip = level ? ~uint64_t{0} : 0;
  for (uint64_t w = at / kWordSamples; w * kWordSamples < stop; ++w) {
    uint64_t other = tail_.word(channel, w) ^ flip;
    if (w == at / kWordSamples) other &= ~uint64_t{0} << (at % kWordSamples);
    if (other == 0) continue;
    const uint64_t found = w * kWordSamples + static_cast<uint64_t>(std::countr_zero(other));
    if (found >= stop) return std::nullopt;
    return tail_.first_sample + found;
  }
  return std::nullopt;
}

std::optional<uint64_t> LiveTailWaveformSource::lastInTail(uint32_t channel, bool level, uint64_t end) const {
  const uint64_t stop = end - tail_.first_sample;
  const uint64_t flip = level ? ~uint64_t{0} : 0;
  for (uint64_t w = (stop + kWordSamples - 1) / kWordSamples; w-- > 0;) {
    uint64_t other = tail_.word(channel, w) ^ flip;
    const uint64_t bits = stop - w * kWordSamples;
    if (bits < kWordSamples) other &= (uint64_t{1} << bits) - 1;
    if (other == 0) continue;
    return tail_.first_sample + w * kWordSamples + (kWordSamples - 1 - static_cast<uint64_t>(std::countl_zero(other)));
  }
  return std::nullopt;
}

wv::ColumnSummary LiveTailWaveformSource::exactRange(uint32_t channel, uint64_t begin, uint64_t end) const {
  const bool first = level(channel, begin);
  const std::optional<uint64_t> edge = nextEdge(channel, begin);
  if (edge && *edge < end) return wv::ColumnSummary{true, true};
  return wv::ColumnSummary{!first, first};
}

}
