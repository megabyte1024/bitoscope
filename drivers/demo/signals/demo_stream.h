/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <algorithm>
#include <bit>
#include <cstring>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <vector>

#include "channel_pack.h"
#include "demo_scenario.h"
#include "line_trace.h"

namespace bitoscope::sim {

enum class WireEncoding {
  ChannelMajor,
  SampleMajor,
};

inline uint32_t demoChunkSamples(uint64_t sample_rate_hz, uint32_t num_channels,
                                 WireEncoding encoding) {
  constexpr uint64_t kTransferBytes = 1024 * 1024;
  const uint64_t channels = std::max<uint32_t>(num_channels, 1);
  const uint64_t by_size = encoding == WireEncoding::ChannelMajor
                               ? kTransferBytes * 8 / channels
                               : kTransferBytes / ((channels + 7) / 8);
  const uint64_t by_time = sample_rate_hz / 50;
  const uint64_t n = std::min(by_size, by_time);
  return static_cast<uint32_t>(std::max<uint64_t>(n & ~uint64_t{7}, 8));
}

struct DemoChunk {
  uint64_t first_sample_index = 0;
  uint32_t num_channels = 0;
  uint32_t num_samples = 0;
  WireEncoding encoding = WireEncoding::ChannelMajor;
  std::vector<uint8_t> payload;
};

class DemoStream {
 public:
  static std::expected<DemoStream, std::string> create(uint64_t sample_rate_hz,
                                                       uint32_t channel_mask,
                                                       WireEncoding encoding,
                                                       uint32_t chunk_samples = 16384) {
    if (channel_mask == 0) {
      return std::unexpected("channel mask selects no channels");
    }
    if (chunk_samples == 0 || chunk_samples % 8 != 0) {
      return std::unexpected("chunk size must be nonzero multiple of 8 samples, got " +
                              std::to_string(chunk_samples));
    }
    auto frame = renderDemoFrame(sample_rate_hz);
    if (!frame.has_value()) {
      return std::unexpected(frame.error());
    }
    DemoStream stream;
    stream.encoding_ = encoding;
    stream.num_channels_ = static_cast<uint32_t>(std::popcount(channel_mask));
    stream.chunk_samples_ = chunk_samples;
    const std::vector<Word> selected = extractChannels(frame->samples, channel_mask);
    stream.frame_samples_ = selected.size();
    if (encoding == WireEncoding::SampleMajor) {
      stream.unit_samples_ = 1;
      stream.packed_ = packSampleMajor(selected, stream.num_channels_);
    } else {
      stream.unit_samples_ = 8;
      stream.packed_ = packChannelMajor(selected, stream.num_channels_).value_or(std::vector<uint8_t>{});
    }
    stream.unit_bytes_ = stream.packed_.size() / (stream.frame_samples_ / stream.unit_samples_);
    return stream;
  }

  DemoChunk nextChunk(uint32_t max_samples = 0) {
    uint32_t n = chunk_samples_;
    if (max_samples != 0) {
      n = std::min(n, max_samples & ~7u);
    }
    DemoChunk chunk;
    chunk.first_sample_index = produced_;
    chunk.num_channels = num_channels_;
    chunk.num_samples = n;
    chunk.encoding = encoding_;
    if (n == 0) {
      return chunk;
    }

    chunk.payload.resize(static_cast<size_t>(payloadBytes(n)));
    copyOut(n, chunk.payload);
    return chunk;
  }

  uint64_t payloadBytes(uint64_t num_samples) const {
    return num_samples / unit_samples_ * unit_bytes_;
  }

  std::expected<void, std::string> writeNext(uint64_t num_samples, std::span<uint8_t> out) {
    if (num_samples == 0 || num_samples % 8 != 0) {
      return std::unexpected("chunk is nonzero multiple of 8 samples, got " +
                             std::to_string(num_samples));
    }
    if (out.size() != payloadBytes(num_samples)) {
      return std::unexpected(std::to_string(num_samples) + " samples take " +
                             std::to_string(payloadBytes(num_samples)) + " bytes, but span holds " +
                             std::to_string(out.size()));
    }
    copyOut(num_samples, out);
    return {};
  }

  uint64_t samplesProduced() const { return produced_; }
  uint32_t numChannels() const { return num_channels_; }

 private:
  DemoStream() = default;

  void copyOut(uint64_t n, std::span<uint8_t> out) {
    size_t pos = static_cast<size_t>(produced_ % frame_samples_) / unit_samples_ * unit_bytes_;
    size_t written = 0;
    while (written < out.size()) {
      const size_t take = std::min(out.size() - written, packed_.size() - pos);
      std::memcpy(out.data() + written, packed_.data() + pos, take);
      written += take;
      pos = (pos + take) % packed_.size();
    }
    produced_ += n;
  }

  WireEncoding encoding_ = WireEncoding::ChannelMajor;
  uint32_t num_channels_ = 0;
  uint32_t chunk_samples_ = 0;
  std::vector<uint8_t> packed_;
  size_t frame_samples_ = 0;
  uint32_t unit_samples_ = 1;
  size_t unit_bytes_ = 0;
  uint64_t produced_ = 0;
};

}
