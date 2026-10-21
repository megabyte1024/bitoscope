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

#include "raw_capture_buffer.h"
#include "waveform_source.h"

namespace bitoscope::waveform_capture_source {

inline constexpr uint64_t kWindowCacheBytesPerChannel = uint64_t{1} << 20;
inline constexpr uint64_t kMinCachedWindows = 4;

uint64_t windowCacheSlots(uint64_t cacheBytesPerChannel, uint64_t windowBytes, uint64_t planWindows);

class CaptureWaveformSource final : public waveform_view::WaveformSource {
 public:
  explicit CaptureWaveformSource(const capture::RawCaptureBuffer& buffer,
                                 uint64_t cacheBytesPerChannel = kWindowCacheBytesPerChannel);
  CaptureWaveformSource(const CaptureWaveformSource&) = delete;
  CaptureWaveformSource& operator=(const CaptureWaveformSource&) = delete;

  uint32_t channelCount() const override;
  uint64_t sampleCount() const override;
  uint64_t sampleRateMilliHz() const override;
  std::expected<void, std::string> summary(uint32_t channel, uint64_t begin, uint64_t end,
                                           std::span<waveform_view::ColumnSummary> out) const override;
  uint64_t summaryBlockSamples() const override;
  uint64_t summaryPendingBefore() const override;
  std::optional<waveform_view::ColumnSummary> blockRange(uint32_t channel, uint64_t begin,
                                                         uint64_t end) const override;
  bool level(uint32_t channel, uint64_t sample) const override;
  std::optional<uint64_t> nextEdge(uint32_t channel, uint64_t from) const override;
  std::optional<uint64_t> previousEdge(uint32_t channel, uint64_t from) const override;

  uint64_t chunksDecoded() const;
  uint64_t chunksDecodedOn(uint32_t channel) const;
  uint64_t cachedWindowBytes() const;
  std::optional<std::string> lastReadError() const;

 private:
  struct Chunk {
    bool filled = false;
    uint64_t index = 0;
    uint64_t samples = 0;
    std::vector<uint8_t> bytes;
  };
  struct Memo {
    bool valid = false;
    uint64_t from = 0;
    std::optional<uint64_t> edge;
  };

  struct alignas(64) ChannelState {
    std::vector<Chunk> slots;
    const Chunk* last = nullptr;
    uint64_t lastIndex = 0;
    uint64_t lastSamples = 0;
    uint64_t chunksDecoded = 0;
    std::optional<std::string> lastReadError;
    uint64_t memoTotal = 0;
    Memo nextMemo;
    Memo previousMemo;
    std::vector<capture::Bucket> scratch;
  };

  ChannelState& stateOf(uint32_t channel) const { return states_[channel]; }

  const Chunk* chunkFor(uint32_t channel, uint64_t sample) const;
  void dropMemosIfGrown(ChannelState& state) const;
  std::optional<uint64_t> scanForward(uint32_t channel, bool level, uint64_t begin, uint64_t end) const;
  std::optional<uint64_t> scanBackward(uint32_t channel, bool level, uint64_t begin, uint64_t end) const;
  std::optional<bool> rangeHasOther(uint32_t channel, bool level, uint64_t begin, uint64_t end) const;

  const capture::RawCaptureBuffer& buffer_;
  uint64_t chunkSamples_;
  uint64_t slotsPerChannel_;

  mutable std::vector<ChannelState> states_;
};

}
