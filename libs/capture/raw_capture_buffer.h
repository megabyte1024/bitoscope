/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

// WHY: Not thread-safe except packWindow(): appends are serialised, packing may run on many threads.

#include <atomic>
#include <cstdint>
#include <expected>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "capture_allocator.h"
#include "live_pyramid.h"
#include "pack_scratch.h"
#include "sample_window.h"

namespace bitoscope::capture {

inline constexpr uint32_t kLz4AcceptPercent = 95;

struct ChannelSegment {
  static constexpr uint32_t kLz4Bit = uint32_t{1} << 31;

  uint64_t storage_offset = 0;
  uint32_t bytes_and_codec = 0;
  uint32_t payload_crc32c = 0;

  ChannelSegment() = default;
  ChannelSegment(SegmentCodec codec, uint64_t offset, uint32_t stored_bytes, uint32_t crc)
      : storage_offset(offset),
        bytes_and_codec(stored_bytes | (codec == SegmentCodec::Lz4 ? kLz4Bit : 0)),
        payload_crc32c(crc) {}

  SegmentCodec codec() const { return (bytes_and_codec & kLz4Bit) != 0 ? SegmentCodec::Lz4 : SegmentCodec::None; }
  uint32_t storedBytes() const { return bytes_and_codec & ~kLz4Bit; }
};
static_assert(sizeof(ChannelSegment) == 16);
static_assert((uint64_t{(std::numeric_limits<uint32_t>::max)()} + 7) / 8 < ChannelSegment::kLz4Bit,
              "no window's segment reaches the codec bit");

enum class AppendOutcome {
  Stored,
  Full,
  NoRoomWhileArmed,
};

enum class StoreMode : uint8_t { Linear, Armed, Settled };

struct LiveHistory {
  uint64_t first_sample = 0;
  uint64_t end_sample = 0;
  uint64_t windows = 0;
  bool operator==(const LiveHistory&) const = default;
};

struct IndexRecord {
  uint64_t first_sample_index = 0;
  uint32_t num_samples = 0;

  uint64_t endSample() const { return first_sample_index + num_samples; }
};
static_assert(sizeof(IndexRecord) == 16);

uint64_t captureIndexWindows(const CapturePlan& plan, uint64_t arena_bytes);

uint64_t captureIndexBytes(uint64_t windows, uint32_t channels);

std::expected<uint64_t, std::string> captureScratchBytes(uint32_t channels, uint32_t samples_per_window);

std::expected<uint64_t, std::string> captureHeldBackBytes(const CapturePlan& plan, uint32_t pack_workers,
                                                          bool pyramid = false);

inline std::function<uint64_t(uint64_t)> pyramidPlanesHeldBack(const CapturePlan& plan) {
  return [channels = plan.channels(), total = plan.totalSamples()](uint64_t arena_bytes) {
    return pyramidPlaneBytes(channels, total, arena_bytes);
  };
}

enum class PyramidHistory : uint8_t { None, Pending, Ready, Failed };

// WHY: Lease must be held until the window is appended: a raw segment is a span over the lease buffer.
struct PackedWindow {
  bitoscope::host::WindowInfo info;
  std::span<const PackedSegment> segments;
  WindowBuckets buckets;
};

class RawCaptureBuffer {
 public:
  static std::expected<RawCaptureBuffer, std::string> create(Reservation&& reservation,
      bool pyramid = false);

  std::expected<AppendOutcome, std::string> appendWindow(bitoscope::host::WindowLease& lease);
  std::expected<AppendOutcome, std::string> appendWindow(bitoscope::host::WindowLease&& lease) {
    return appendWindow(lease);
  }
  std::expected<AppendOutcome, std::string> appendWindow(bitoscope::host::WindowLease& lease,
      ArenaBlockSupply& supply);

  std::expected<PackedWindow, std::string> packWindow(bitoscope::host::WindowLease& lease,
      PackScratch& scratch) const;

  std::expected<AppendOutcome, std::string> appendPacked(const PackedWindow& packed,
      ArenaBlockSupply* supply = nullptr);

  std::expected<void, std::string> armHistory(uint64_t retain_samples);

  std::expected<AppendOutcome, std::string> appendArmed(const PackedWindow& packed);

  std::expected<AppendOutcome, std::string> appendWindowArmed(bitoscope::host::WindowLease& lease);

  LiveHistory liveHistory() const {
    const uint64_t windows = windowCount();
    if (windows == 0) return {};
    return {record(0).first_sample_index, record(windows - 1).endSample(), windows};
  }

  std::expected<uint64_t, std::string> settleHistory(uint64_t requested_begin);

  StoreMode storeMode() const { return mode_; }

  uint64_t origin() const { return origin_; }

  std::string describeSample(uint64_t store_sample) const;

  std::expected<void, std::string> read(uint32_t channel, uint64_t first_byte,
      std::span<uint8_t> out) const;

  std::expected<bool, std::string> readWholeWindow(uint32_t channel, uint64_t first_sample,
      std::span<uint8_t> out) const;

  SegmentCodec segmentCodec(uint64_t window_index, uint32_t channel) const {
    return segment(window_index, channel).codec();
  }
  uint32_t segmentStoredBytes(uint64_t window_index, uint32_t channel) const {
    return segment(window_index, channel).storedBytes();
  }
  uint64_t segmentStorageOffset(uint64_t window_index, uint32_t channel) const {
    return segment(window_index, channel).storage_offset;
  }
  uint32_t segmentCrc32c(uint64_t window_index, uint32_t channel) const {
    return segment(window_index, channel).payload_crc32c;
  }

  std::expected<void, std::string> finalize() {
    finalized_ = true;
    return {};
  }

  std::expected<Reservation, std::string> releaseReservation();

  std::span<const IndexRecord> indexRange(uint64_t first, uint64_t end) const;

  uint64_t windowCount() const {
    return mode_ == StoreMode::Armed ? rec_count_ : uint64_t{records_.size()};
  }
  uint64_t sampleCount() const {
    const uint64_t windows = windowCount();
    return windows == 0 ? 0 : record(windows - 1).endSample();
  }
  uint64_t storedSamples() const { return stored_samples_; }
  uint64_t overflowSamples() const { return overflow_samples_; }
  uint64_t firstAvailableSample() const {
    return mode_ == StoreMode::Armed && rec_count_ != 0 ? record(0).first_sample_index : 0;
  }
  bool finalized() const { return finalized_; }
  const CapturePlan& plan() const { return reservation_.plan; }

  // WHY: Readers are lock-free: writer stores published_ with release, readers load it with acquire.
  uint64_t readableSampleCount() const {
    const std::span<const IndexRecord> records = publishedRecords();
    return records.empty() ? 0 : records.back().endSample();
  }

  bool hasPyramid() const { return pyramid_ != nullptr; }
  PyramidHistory pyramidHistory() const { return history_.value.load(std::memory_order_acquire); }
  uint64_t summaryPendingBefore() const {
    if (history_.value.load(std::memory_order_acquire) != PyramidHistory::Pending) return 0;
    return history_end_.value.load(std::memory_order_relaxed);
  }

  std::expected<void, std::string> summarizeHistory(const std::atomic<bool>& stop);

  std::unexpected<std::string> failHistorySummary(std::string why);

  std::expected<uint64_t, std::string> releaseUnusedBlocks();
  bool blocksReleased() const { return blocks_released_; }
  uint64_t pyramidBytes() const { return pyramid_ ? pyramid_->memoryBytes() : 0; }
  uint8_t pyramidPlaneByteForTest(uint32_t channel, size_t level, uint64_t byte, bool ones) const {
    return pyramid_ ? pyramid_->planeByteForTest(channel, level, byte, ones) : uint8_t{0};
  }
  size_t pyramidLevelsForTest() const { return pyramid_ ? pyramid_->levelCount() : 0; }
  std::expected<void, std::string> pyramidSummary(uint32_t channel, uint64_t begin, uint64_t end,
      std::span<Bucket> out) const;

  std::optional<Bucket> pyramidRangeExact(uint32_t channel, uint64_t begin, uint64_t end) const;

  uint64_t arenaBytes() const { return reservation_.arenaBytes(); }
  uint64_t arenaUsedBytes() const {
    return wrapped_ ? reservation_.arenaBytes() - head_ + cursor_ : cursor_ - head_;
  }

  uint64_t indexReservedBytesForTest() const {
    return uint64_t{records_.capacity()} * sizeof(IndexRecord) +
           uint64_t{segments_.capacity()} * sizeof(ChannelSegment);
  }

  uint64_t segmentDecodeCallsForTest() const {
    return segment_decode_calls_.value.load(std::memory_order_relaxed);
  }

 private:
  RawCaptureBuffer() = default;

  std::expected<std::vector<uint8_t>, std::string> decodeSegment(size_t window_index,
      uint32_t channel, uint64_t window_bytes) const;

  std::expected<void, std::string> decodeSegmentInto(size_t window_index, uint32_t channel,
      std::span<uint8_t> decoded) const;

  std::optional<uint64_t> ringPlace(uint64_t at, uint64_t bytes, bool& wrapped) const;

  friend struct RingPlaceTestAccess;

  struct Trial {
    uint64_t cursor = 0;
    bool wrapped = false;
  };

  std::optional<Trial> trialPlace(const PackedWindow& packed);

  std::expected<bool, std::string> takePlacedBlocks(uint64_t first_sample,
      ArenaBlockSupply* supply);

  void storePlaced(const PackedWindow& packed, const Trial& trial, uint64_t slot);

  void prepareNextBlock(ArenaBlockSupply& supply, uint64_t stored_end) const;

  size_t physical(uint64_t window) const {
    return static_cast<size_t>(mode_ == StoreMode::Armed ? (rec_head_ + window) % max_windows_
                                                         : window);
  }
  const IndexRecord& record(uint64_t window) const { return records_[physical(window)]; }
  const ChannelSegment& segment(uint64_t window, uint32_t channel) const {
    return segments_[physical(window) * reservation_.plan.channels() + channel];
  }

  void retireHistory(uint64_t first, uint64_t end);

  void evictOldest();

  void flattenIndex();

  std::expected<void, std::string> checkPacked(const PackedWindow& packed,
      uint64_t stored_end) const;

  std::expected<void, std::string> checkWindowInfo(const bitoscope::host::WindowInfo& info) const;

  Reservation reservation_;
  std::vector<IndexRecord> records_;
  std::vector<ChannelSegment> segments_;
  PackScratch scratch_;
  std::vector<uint64_t> placed_;
  uint64_t compress_bound_ = 0;
  uint64_t cursor_ = 0;
  uint64_t head_ = 0;
  bool wrapped_ = false;
  uint64_t max_windows_ = 0;
  StoreMode mode_ = StoreMode::Linear;
  uint64_t retain_samples_ = 0;
  uint64_t rec_head_ = 0;
  uint64_t rec_count_ = 0;
  uint64_t armed_end_ = 0;
  uint64_t stored_samples_ = 0;
  uint64_t origin_ = 0;
  uint64_t overflow_samples_ = 0;
  bool finalized_ = false;
  bool released_ = false;
  bool blocks_released_ = false;

  struct PublishedCount {
    std::atomic<uint64_t> value{0};
    PublishedCount() = default;
    PublishedCount(PublishedCount&& other) noexcept : value(other.value.load(std::memory_order_relaxed)) {}
    PublishedCount& operator=(PublishedCount&& other) noexcept {
      value.store(other.value.load(std::memory_order_relaxed), std::memory_order_relaxed);
      return *this;
    }
    uint64_t load(std::memory_order order) const { return value.load(order); }
    void store(uint64_t count) { value.store(count, std::memory_order_release); }
  };
  static constexpr uint64_t kNotReadable = (std::numeric_limits<uint64_t>::max)();
  PublishedCount published_;
  std::unique_ptr<LivePyramid> pyramid_;
  uint64_t pyramid_capacity_ = 0;

  template <typename T>
  struct MovableAtomic {
    std::atomic<T> value;
    explicit MovableAtomic(T initial) : value(initial) {}
    MovableAtomic(MovableAtomic&& other) noexcept : value(other.value.load(std::memory_order_relaxed)) {}
    MovableAtomic& operator=(MovableAtomic&& other) noexcept {
      value.store(other.value.load(std::memory_order_relaxed), std::memory_order_relaxed);
      return *this;
    }
  };
  MovableAtomic<bool> buckets_live_{true};
  uint64_t history_windows_ = 0;
  MovableAtomic<uint64_t> history_end_{0};
  MovableAtomic<PyramidHistory> history_{PyramidHistory::None};
  std::string history_error_;
  std::vector<uint8_t> history_raw_;
  std::vector<uint8_t> history_buckets_;
  const IndexRecord* live_records_ = nullptr;
  const ChannelSegment* live_segments_ = nullptr;
  mutable MovableAtomic<uint64_t> segment_decode_calls_{0};

  std::span<const IndexRecord> publishedRecords() const {
    const uint64_t n = published_.load(std::memory_order_acquire);
    if (n == kNotReadable || n == 0) return {};
    return {live_records_, static_cast<size_t>(n)};
  }
};

}
