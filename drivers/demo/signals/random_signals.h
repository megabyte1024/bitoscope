/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <bit>
#include <cstdint>
#include <cstring>
#include <expected>
#include <span>
#include <string>
#include <vector>

#include "channel_pack.h"
#include "demo_stream.h"
#include "line_trace.h"

namespace bitoscope::sim {

inline uint64_t splitmix64Next(uint64_t& state) {
  state += 0x9E3779B97F4A7C15ull;
  uint64_t z = state;
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
  return z ^ (z >> 31);
}

inline Word randomWordAt(uint64_t seed, uint64_t sample_index) {
  uint64_t state = seed ^ (sample_index * 0x9E3779B97F4A7C15ull + 0xD1B54A32D192ED03ull);
  return static_cast<Word>(splitmix64Next(state) & 0xFFFFFFFFull);
}

class RandomStream {
 public:
  static std::expected<RandomStream, std::string> create(uint64_t seed, uint32_t channel_mask,
                                                          WireEncoding encoding,
                                                          uint32_t chunk_samples = 16384) {
    if (channel_mask == 0) {
      return std::unexpected("channel mask selects no channels");
    }
    if (chunk_samples == 0 || chunk_samples % 8 != 0) {
      return std::unexpected("chunk size must be nonzero multiple of 8 samples, got " +
                              std::to_string(chunk_samples));
    }
    RandomStream stream;
    stream.seed_ = seed;
    stream.channel_mask_ = channel_mask;
    stream.encoding_ = encoding;
    stream.num_channels_ = static_cast<uint32_t>(std::popcount(channel_mask));
    stream.chunk_samples_ = chunk_samples;
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

    chunk.payload = pack(n);
    produced_ += n;
    return chunk;
  }

  uint64_t payloadBytes(uint64_t num_samples) const {
    return encoding_ == WireEncoding::ChannelMajor
               ? num_samples / 8 * num_channels_
               : num_samples * ((uint64_t{num_channels_} + 7) / 8);
  }

  std::expected<void, std::string> writeNext(uint64_t num_samples, std::span<uint8_t> out) {
    if (num_samples == 0 || num_samples % 8 != 0 || num_samples > 0xFFFFFFF8u) {
      return std::unexpected("chunk is nonzero multiple of 8 samples, got " +
                             std::to_string(num_samples));
    }
    if (out.size() != payloadBytes(num_samples)) {
      return std::unexpected(std::to_string(num_samples) + " samples take " +
                             std::to_string(payloadBytes(num_samples)) + " bytes, but span holds " +
                             std::to_string(out.size()));
    }
    const std::vector<uint8_t> packed = pack(static_cast<uint32_t>(num_samples));
    std::memcpy(out.data(), packed.data(), packed.size());
    produced_ += num_samples;
    return {};
  }

  uint64_t samplesProduced() const { return produced_; }
  uint32_t numChannels() const { return num_channels_; }

 private:
  RandomStream() = default;

  std::vector<uint8_t> pack(uint32_t n) const {
    std::vector<Word> words(n);
    for (uint32_t i = 0; i < n; ++i) {
      words[i] = randomWordAt(seed_, produced_ + i);
    }
    const std::vector<Word> selected = extractChannels(words, channel_mask_);
    if (encoding_ == WireEncoding::SampleMajor) {
      return packSampleMajor(selected, num_channels_);
    }
    return packChannelMajor(selected, num_channels_).value_or(std::vector<uint8_t>{});
  }

  uint64_t seed_ = 0;
  uint32_t channel_mask_ = 0;
  WireEncoding encoding_ = WireEncoding::ChannelMajor;
  uint32_t num_channels_ = 0;
  uint32_t chunk_samples_ = 0;
  uint64_t produced_ = 0;
};

}
