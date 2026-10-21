/*
 * SPDX-License-Identifier: MIT
 */

#include "capture_waveform_source.h"

#include <algorithm>
#include <bit>

#include "pyramid.h"

namespace bitoscope::waveform_capture_source {

namespace {

constexpr uint64_t kBucket = capture::Pyramid::kBaseBucketSamples;
constexpr uint64_t kFanout = capture::Pyramid::kFanout;

uint64_t roundUp(uint64_t value, uint64_t step) { return (value + step - 1) / step * step; }

}

uint64_t windowCacheSlots(uint64_t cacheBytesPerChannel, uint64_t windowBytes, uint64_t planWindows) {
  const uint64_t fit = windowBytes == 0 ? planWindows : cacheBytesPerChannel / windowBytes;
  return (std::min)((std::max)(kMinCachedWindows, fit), planWindows);
}

CaptureWaveformSource::CaptureWaveformSource(const capture::RawCaptureBuffer& buffer, uint64_t cacheBytesPerChannel)
    : buffer_(buffer),
      chunkSamples_((std::max)(uint64_t{8}, uint64_t{buffer.plan().samplesPerWindow()})),
      slotsPerChannel_(windowCacheSlots(cacheBytesPerChannel, chunkSamples_ / 8,
                                        (buffer.plan().totalSamples() + chunkSamples_ - 1) / chunkSamples_)),
      states_(buffer.plan().channels()) {
  for (ChannelState& state : states_) state.slots.resize(static_cast<size_t>(slotsPerChannel_));
}

uint64_t CaptureWaveformSource::chunksDecoded() const {
  uint64_t total = 0;
  for (const ChannelState& state : states_) total += state.chunksDecoded;
  return total;
}

uint64_t CaptureWaveformSource::chunksDecodedOn(uint32_t channel) const {
  return channel < states_.size() ? states_[channel].chunksDecoded : 0;
}

uint64_t CaptureWaveformSource::cachedWindowBytes() const {
  uint64_t total = 0;
  for (const ChannelState& state : states_) {
    for (const Chunk& slot : state.slots) total += slot.filled ? slot.bytes.size() : 0;
  }
  return total;
}

std::optional<std::string> CaptureWaveformSource::lastReadError() const {
  for (const ChannelState& state : states_) {
    if (state.lastReadError.has_value()) return state.lastReadError;
  }
  return std::nullopt;
}

uint32_t CaptureWaveformSource::channelCount() const { return buffer_.plan().channels(); }

uint64_t CaptureWaveformSource::sampleCount() const { return buffer_.readableSampleCount(); }

uint64_t CaptureWaveformSource::sampleRateMilliHz() const { return buffer_.plan().sampleRateMilliHz(); }

std::expected<void, std::string> CaptureWaveformSource::summary(uint32_t channel, uint64_t begin, uint64_t end,
                                                                std::span<waveform_view::ColumnSummary> out) const {
  if (channel >= channelCount()) {
    return std::unexpected("channel " + std::to_string(channel) + " does not exist");
  }
  std::vector<capture::Bucket>& scratch = stateOf(channel).scratch;
  scratch.assign(out.size(), capture::Bucket{});
  if (auto summarized = buffer_.pyramidSummary(channel, begin, end, scratch); !summarized) {
    return std::unexpected(summarized.error());
  }
  for (size_t j = 0; j < out.size(); ++j) {
    out[j] = waveform_view::ColumnSummary{scratch[j].has_zero, scratch[j].has_one};
  }
  return {};
}

uint64_t CaptureWaveformSource::summaryBlockSamples() const { return buffer_.hasPyramid() ? kBucket : 0; }

uint64_t CaptureWaveformSource::summaryPendingBefore() const { return buffer_.summaryPendingBefore(); }

std::optional<waveform_view::ColumnSummary> CaptureWaveformSource::blockRange(uint32_t channel, uint64_t begin,
                                                                             uint64_t end) const {
  if (channel >= channelCount()) return std::nullopt;
  const std::optional<capture::Bucket> exact = buffer_.pyramidRangeExact(channel, begin, end);
  if (!exact.has_value()) return std::nullopt;
  return waveform_view::ColumnSummary{exact->has_zero, exact->has_one};
}

const CaptureWaveformSource::Chunk* CaptureWaveformSource::chunkFor(uint32_t channel, uint64_t sample) const {
  const uint64_t total = sampleCount();
  if (channel >= states_.size() || sample >= total) return nullptr;
  const uint64_t index = sample / chunkSamples_;
  const uint64_t first = index * chunkSamples_;
  const uint64_t samples = (std::min)(chunkSamples_, total - first);
  ChannelState& state = stateOf(channel);
  if (state.lastIndex == index && state.lastSamples == samples) return state.last;
  Chunk& slot = state.slots[static_cast<size_t>(index % slotsPerChannel_)];
  const auto remember = [&] {
    state.last = &slot;
    state.lastIndex = index;
    state.lastSamples = samples;
    return &slot;
  };
  if (slot.filled && slot.index == index && slot.samples == samples) return remember();
  state.lastSamples = 0;
  slot.filled = false;
  slot.bytes.resize(static_cast<size_t>((samples + 7) / 8));
  auto whole = buffer_.readWholeWindow(channel, first, slot.bytes);
  if (whole.has_value() && !*whole) {
    whole = buffer_.read(channel, first / 8, slot.bytes).transform([] { return true; });
  }
  if (!whole) {
    state.lastReadError = std::move(whole.error());
    return nullptr;
  }
  ++state.chunksDecoded;
  slot.index = index;
  slot.samples = samples;
  slot.filled = true;
  return remember();
}

bool CaptureWaveformSource::level(uint32_t channel, uint64_t sample) const {
  const Chunk* chunk = chunkFor(channel, sample);
  if (chunk == nullptr) return false;
  const uint64_t at = sample - chunk->index * chunkSamples_;
  return ((chunk->bytes[static_cast<size_t>(at / 8)] >> (at % 8)) & 1u) != 0;
}

std::optional<uint64_t> CaptureWaveformSource::scanForward(uint32_t channel, bool level, uint64_t begin,
                                                           uint64_t end) const {
  const uint8_t flat = level ? 0xFF : 0x00;
  uint64_t s = begin;
  while (s < end) {
    const Chunk* chunk = chunkFor(channel, s);
    if (chunk == nullptr) return std::nullopt;
    const uint64_t first = chunk->index * chunkSamples_;
    const uint64_t stop = (std::min)(end, first + chunk->samples);
    while (s < stop) {
      const uint64_t at = s - first;
      const uint8_t byte = chunk->bytes[static_cast<size_t>(at / 8)];
      if (at % 8 == 0 && s + 8 <= stop) {
        if (byte != flat) {
          return s + static_cast<uint64_t>(std::countr_zero(static_cast<unsigned>(byte ^ flat)));
        }
        s += 8;
        continue;
      }
      if ((((byte >> (at % 8)) & 1u) != 0) != level) return s;
      ++s;
    }
  }
  return std::nullopt;
}

std::optional<uint64_t> CaptureWaveformSource::scanBackward(uint32_t channel, bool level, uint64_t begin,
                                                            uint64_t end) const {
  const uint8_t flat = level ? 0xFF : 0x00;
  uint64_t s = end;
  while (s > begin) {
    const Chunk* chunk = chunkFor(channel, s - 1);
    if (chunk == nullptr) return std::nullopt;
    const uint64_t first = chunk->index * chunkSamples_;
    const uint64_t stop = (std::max)(begin, first);
    while (s > stop) {
      const uint64_t at = s - 1 - first;
      const uint8_t byte = chunk->bytes[static_cast<size_t>(at / 8)];
      if (at % 8 == 7 && s - 8 >= stop) {
        if (byte != flat) {
          return s - 1 - static_cast<uint64_t>(std::countl_zero(static_cast<uint8_t>(byte ^ flat)));
        }
        s -= 8;
        continue;
      }
      if ((((byte >> (at % 8)) & 1u) != 0) != level) return s - 1;
      --s;
    }
  }
  return std::nullopt;
}

std::optional<bool> CaptureWaveformSource::rangeHasOther(uint32_t channel, bool level, uint64_t begin,
                                                         uint64_t end) const {
  std::vector<capture::Bucket>& scratch = stateOf(channel).scratch;
  scratch.assign(1, capture::Bucket{});
  if (!buffer_.pyramidSummary(channel, begin, end, scratch)) return std::nullopt;
  return level ? scratch[0].has_zero : scratch[0].has_one;
}

void CaptureWaveformSource::dropMemosIfGrown(ChannelState& state) const {
  const uint64_t total = sampleCount();
  if (total != state.memoTotal) {
    state.memoTotal = total;
    state.nextMemo = Memo{};
    state.previousMemo = Memo{};
  }
}

std::optional<uint64_t> CaptureWaveformSource::nextEdge(uint32_t channel, uint64_t from) const {
  const uint64_t total = sampleCount();
  if (channel >= channelCount() || from >= total || from + 1 >= total) return std::nullopt;
  ChannelState& state = stateOf(channel);
  dropMemosIfGrown(state);
  Memo& memo = state.nextMemo;
  if (memo.valid && memo.from <= from && (!memo.edge.has_value() || from < *memo.edge)) return memo.edge;

  const bool current = level(channel, from);
  const auto remember = [&](std::optional<uint64_t> edge) {
    memo = Memo{true, from, edge};
    return edge;
  };

  uint64_t s = from + 1;
  const uint64_t bucketEnd = (std::min)(roundUp(s, kBucket), total);
  if (auto edge = scanForward(channel, current, s, bucketEnd)) return remember(edge);
  s = bucketEnd;

  while (s < total) {
    uint64_t span = kBucket;
    while (s % (span * kFanout) == 0 && s + span * kFanout <= total) span *= kFanout;
    for (;;) {
      const uint64_t end = (std::min)(s + span, total);
      if (end - s < kBucket) return remember(scanForward(channel, current, s, total));
      const std::optional<bool> other = rangeHasOther(channel, current, s, end);
      if (!other.has_value()) return remember(scanForward(channel, current, s, total));
      if (!*other) {
        s = end;
        break;
      }
      if (span == kBucket) return remember(scanForward(channel, current, s, end));
      span /= kFanout;
    }
  }
  return remember(std::nullopt);
}

std::optional<uint64_t> CaptureWaveformSource::previousEdge(uint32_t channel, uint64_t from) const {
  const uint64_t total = sampleCount();
  if (channel >= channelCount() || total == 0) return std::nullopt;
  from = (std::min)(from, total - 1);
  if (from == 0) return std::nullopt;
  ChannelState& state = stateOf(channel);
  dropMemosIfGrown(state);
  Memo& memo = state.previousMemo;
  if (memo.valid && from <= memo.from && (!memo.edge.has_value() || from >= *memo.edge)) return memo.edge;

  const bool current = level(channel, from);
  const auto remember = [&](std::optional<uint64_t> edge) {
    memo = Memo{true, from, edge};
    return edge;
  };
  const auto after = [](std::optional<uint64_t> sample) {
    return sample.has_value() ? std::optional<uint64_t>(*sample + 1) : std::nullopt;
  };

  uint64_t s = from - from % kBucket;
  if (auto found = scanBackward(channel, current, s, from)) return remember(after(found));

  while (s > 0) {
    uint64_t span = kBucket;
    while (s % (span * kFanout) == 0 && s >= span * kFanout) span *= kFanout;
    for (;;) {
      const uint64_t begin = s - span;
      const std::optional<bool> other = rangeHasOther(channel, current, begin, s);
      if (!other.has_value()) return remember(after(scanBackward(channel, current, 0, s)));
      if (!*other) {
        s = begin;
        break;
      }
      if (span == kBucket) return remember(after(scanBackward(channel, current, begin, s)));
      span /= kFanout;
    }
  }
  return remember(std::nullopt);
}

}
