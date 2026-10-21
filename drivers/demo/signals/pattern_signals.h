/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

// WHY: This algorithm is mirrored by an independent Python capture checker;
// change both and their golden values, or neither.

#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "demo_stream.h"

namespace bitoscope::sim {

inline constexpr uint64_t kPatternPeriodBytes = 600011;
inline constexpr uint64_t kPatternPeriodSamples = 8 * kPatternPeriodBytes;
inline constexpr uint64_t kPatternWindowSamples = 1'048'576;

constexpr bool isPrime(uint64_t n) {
  if (n < 2) return false;
  if (n % 2 == 0) return n == 2;
  for (uint64_t d = 3; d <= n / d; d += 2) {
    if (n % d == 0) return false;
  }
  return true;
}
static_assert(isPrime(kPatternPeriodBytes), "the pattern period must be a prime number of bytes");
static_assert(kPatternPeriodSamples >= 4 * kPatternWindowSamples,
              "the pattern period must be several windows long");
static_assert(32 * kPatternPeriodBytes <= 20'000'000, "the 32-channel table must stay within ~20 MB");

struct PatternTrain {
  bool first_level = false;
  std::vector<uint64_t> lengths;
};

PatternTrain patternTrain(uint64_t seed, uint32_t physical_channel);

inline constexpr uint64_t kNoiseChannelSalt = 0xA0761D6478BD642Full;

std::vector<uint8_t> noisePeriod(uint64_t seed, uint32_t physical_channel);

struct PatternMarker {
  uint32_t physical_channel = 0;
  uint64_t at_sample = 0;
};

inline constexpr uint64_t kMaxMarkerSample = 1'000'000'000'000'000'000ull;

enum class ReplayContent {
  PulseTrain,
  Noise,
};

class PatternStream {
 public:
  static std::expected<PatternStream, std::string> create(uint64_t seed, uint32_t channel_mask,
                                                          WireEncoding encoding,
                                                          uint32_t chunk_samples = 16384,
                                                          std::optional<PatternMarker> marker = std::nullopt);

  static std::expected<PatternStream, std::string> createNoise(uint64_t seed, uint32_t channel_mask,
                                                               WireEncoding encoding,
                                                               uint32_t chunk_samples = 16384,
                                                               std::optional<PatternMarker> marker = std::nullopt);

  DemoChunk nextChunk(uint32_t max_samples = 0);

  uint64_t payloadBytes(uint64_t num_samples) const;

  std::expected<void, std::string> writeNext(uint64_t num_samples, std::span<uint8_t> out);

  std::expected<void, std::string> writeAt(uint64_t first_sample, uint64_t num_samples,
                                           std::span<uint8_t> out) const;

  std::span<const uint8_t> table() const { return table_; }

  uint64_t samplesProduced() const { return produced_; }
  uint32_t numChannels() const { return num_channels_; }
  WireEncoding encoding() const { return encoding_; }
  ReplayContent content() const { return content_; }

 private:
  PatternStream() = default;

  static std::expected<PatternStream, std::string> build(ReplayContent content, uint64_t seed,
                                                         uint32_t channel_mask, WireEncoding encoding,
                                                         uint32_t chunk_samples,
                                                         std::optional<PatternMarker> marker);

  size_t offsetOf(uint64_t first) const;

  void copyOut(const std::vector<uint8_t>& table, uint64_t first, uint64_t num_samples,
               std::span<uint8_t> out) const;

  WireEncoding encoding_ = WireEncoding::ChannelMajor;
  ReplayContent content_ = ReplayContent::PulseTrain;
  uint32_t num_channels_ = 0;
  uint32_t chunk_samples_ = 0;
  std::vector<uint8_t> table_;
  std::vector<uint8_t> table_high_;
  std::optional<PatternMarker> marker_;
  uint32_t marker_wire_channel_ = 0;
  uint64_t produced_ = 0;
};

}
