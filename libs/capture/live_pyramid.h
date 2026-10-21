/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <atomic>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "pyramid.h"

namespace bitoscope::capture {

struct WindowBuckets {
  uint64_t first_bucket = 0;
  uint32_t count = 0;
  uint32_t stride = 0;
  std::span<const uint8_t> bits;
};

inline constexpr uint8_t kBucketHasOne = 1;
inline constexpr uint8_t kBucketHasZero = 2;

uint32_t maxWindowBuckets(uint32_t samples_per_window);
uint64_t windowBucketBytes(uint32_t channels, uint32_t samples_per_window);
uint64_t pyramidHistoryScratchBytes(uint32_t channels, uint32_t samples_per_window);

uint32_t windowBucketBits(uint64_t first_sample, uint32_t num_samples, std::span<const uint8_t> raw,
                          std::span<uint8_t> out);

uint64_t pyramidSamplesBound(uint32_t channels, uint64_t arena_bytes);
uint64_t pyramidCapacity(uint32_t channels, uint64_t total_samples, uint64_t arena_bytes);
uint64_t pyramidPlaneBytes(uint32_t channels, uint64_t total_samples, uint64_t arena_bytes);

class LivePyramid {
 public:
  static uint64_t bytesFor(uint32_t channels, uint64_t capacity_samples);

  static std::expected<std::unique_ptr<LivePyramid>, std::string> create(uint32_t channels,
                                                                        uint64_t capacity_samples);

  void fold(const WindowBuckets& window, bool shared = false);

  // WHY: summary() and exactRange() run on any thread with no lock while fold() of later windows proceeds.
  std::expected<void, std::string> summary(uint32_t channel, uint64_t begin, uint64_t end, uint64_t available,
                                           std::span<Bucket> out) const;

  std::optional<Bucket> exactRange(uint32_t channel, uint64_t begin, uint64_t end, uint64_t available) const;

  uint64_t memoryBytes() const { return uint64_t{bytes_per_channel_} * channels_ * 2; }
  size_t levelCount() const { return levels_.size(); }

  uint8_t planeByteForTest(uint32_t channel, size_t level, uint64_t byte, bool ones) const {
    if (channel >= channels_ || level >= levels_.size() || byte >= (levels_[level].buckets + 7) / 8) return 0;
    return loadByte((ones ? ones_ : zeros_).get(), index(channel, levels_[level], byte));
  }

 private:
  struct Level {
    uint64_t buckets = 0;
    uint64_t offset = 0;
  };

  LivePyramid() = default;
  size_t index(uint32_t channel, const Level& level, uint64_t byte) const {
    return static_cast<size_t>(uint64_t{channel} * bytes_per_channel_ + level.offset + byte);
  }
  struct FreePlane {
    void operator()(uint8_t* plane) const noexcept;
  };
  using Plane = std::unique_ptr<uint8_t[], FreePlane>;

  static uint8_t loadByte(const uint8_t* plane, size_t at) {
    return std::atomic_ref<uint8_t>(const_cast<uint8_t&>(plane[at])).load(std::memory_order_relaxed);
  }
  template <bool Shared>
  void foldWith(const WindowBuckets& window);
  template <bool Shared>
  static void setBit(uint8_t* plane, size_t at, uint8_t bit);
  bool anyBit(const uint8_t* plane, uint32_t channel, const Level& level, uint64_t first, uint64_t last) const;

  uint32_t channels_ = 0;
  std::vector<Level> levels_;
  uint64_t bytes_per_channel_ = 0;
  Plane ones_;
  Plane zeros_;
};

static_assert(std::atomic_ref<uint8_t>::is_always_lock_free);
static_assert(std::atomic_ref<uint8_t>::required_alignment == alignof(uint8_t));

}
