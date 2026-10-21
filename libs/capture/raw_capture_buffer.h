/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

// WHY: Not thread-safe except packWindow(): appends are serialised, packing may run on many threads.

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <expected>
#include <format>
#include <functional>
#include <limits>
#include <new>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "capture_allocator.h"
#include "crc32c.h"
#include "lz4_block.h"
#include "live_pyramid.h"
#include "pack_scratch.h"
#include "sample_window.h"

namespace bitoscope::capture {

inline constexpr uint32_t kLz4AcceptPercent = 95;

struct ChannelSegment {
  SegmentCodec codec = SegmentCodec::None;
  uint64_t storage_offset = 0;
  uint32_t stored_bytes = 0;
  uint32_t payload_crc32c = 0;
};

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
  uint64_t gap_samples_before = 0;

  uint64_t endSample() const { return first_sample_index + num_samples; }
};

uint64_t captureIndexWindows(const CapturePlan& plan, uint64_t arena_bytes);

uint64_t captureIndexBytes(uint64_t windows, uint32_t channels);

inline std::expected<uint64_t, std::string> captureScratchBytes(uint32_t channels,
                                                                uint32_t samples_per_window) {
  const uint64_t max_channel_bytes = (static_cast<uint64_t>(samples_per_window) + 7) / 8;
  if (max_channel_bytes > (std::numeric_limits<size_t>::max)()) {
    return std::unexpected("capture's compression scratch does not fit this platform");
  }
  auto bound = bitoscope::host::lz4CompressBound(static_cast<size_t>(max_channel_bytes));
  if (!bound) return std::unexpected(bound.error());
  const uint64_t per_channel = *bound;
  if (channels != 0 && per_channel > (std::numeric_limits<uint64_t>::max)() / channels) {
    return std::unexpected("capture's compression scratch does not fit this platform");
  }
  return per_channel * channels;
}

inline std::expected<uint64_t, std::string> captureHeldBackBytes(const CapturePlan& plan,
                                                                 uint32_t pack_workers, bool pyramid = false) {
  constexpr uint64_t kMax = (std::numeric_limits<uint64_t>::max)();
  const uint32_t channels = plan.channels();
  const uint32_t samples_per_window = plan.samplesPerWindow();
  auto scratch = captureScratchBytes(channels, samples_per_window);
  if (!scratch) return std::unexpected(scratch.error());
  const uint64_t pool =
      windowPoolBytes(windowPoolBuffers(pack_workers), channels, samples_per_window);
  const uint64_t scratches = 1 + packPoolDepth(pack_workers);
  const uint64_t scratch_bytes = *scratch > kMax / scratches ? kMax : *scratch * scratches;
  const uint64_t index = captureIndexBytes(captureIndexWindows(plan, kMax), channels);
  const uint64_t outside = pool > kMax - scratch_bytes ? kMax : pool + scratch_bytes;
  const uint64_t held = outside > kMax - index ? kMax : outside + index;
  if (!pyramid) return held;
  const uint64_t buckets = windowBucketBytes(channels, samples_per_window) * scratches;
  const uint64_t pass = pyramidHistoryScratchBytes(channels, samples_per_window);
  const uint64_t extra = buckets > kMax - pass ? kMax : buckets + pass;
  return held > kMax - extra ? kMax : held + extra;
}

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
  static std::expected<RawCaptureBuffer, std::string> create(Reservation&& reservation, bool pyramid = false) {
    if (reservation.blocks.empty() || reservation.arenaBytes() == 0) {
      return std::unexpected("capture buffer needs non-empty arena");
    }
    RawCaptureBuffer buffer;
    buffer.reservation_ = std::move(reservation);
    const CapturePlan& plan = buffer.reservation_.plan;
    auto scratch = PackScratch::create(plan.channels(), plan.samplesPerWindow(), pyramid);
    if (!scratch) return std::unexpected(scratch.error());
    buffer.scratch_ = std::move(*scratch);
    if (pyramid) {
      buffer.pyramid_capacity_ =
          pyramidCapacity(plan.channels(), plan.totalSamples(), buffer.reservation_.arenaBytes());
      auto made = LivePyramid::create(plan.channels(), buffer.pyramid_capacity_);
      if (!made) return std::unexpected(made.error());
      buffer.pyramid_ = std::move(*made);
      try {
        buffer.history_raw_.resize(static_cast<size_t>((uint64_t{plan.samplesPerWindow()} + 7) / 8));
        buffer.history_buckets_.resize(static_cast<size_t>(windowBucketBytes(plan.channels(), plan.samplesPerWindow())));
      } catch (const std::bad_alloc&) {
        return std::unexpected("out of memory for pyramid's history scratch");
      }
    }
    buffer.compress_bound_ = buffer.scratch_.region_bytes_;
    buffer.max_windows_ = captureIndexWindows(plan, buffer.reservation_.arenaBytes());
    try {
      buffer.records_.reserve(static_cast<size_t>(buffer.max_windows_));
      buffer.segments_.reserve(static_cast<size_t>(buffer.max_windows_) * plan.channels());
      buffer.placed_.resize(plan.channels());
    } catch (const std::bad_alloc&) {
      return std::unexpected("out of memory reserving capture index");
    }
    buffer.live_records_ = buffer.records_.data();
    buffer.live_segments_ = buffer.segments_.data();
    return buffer;
  }

  std::expected<AppendOutcome, std::string> appendWindow(bitoscope::host::WindowLease& lease) {
    auto packed = packWindow(lease, scratch_);
    if (!packed) return std::unexpected(packed.error());
    return appendPacked(*packed);
  }
  std::expected<AppendOutcome, std::string> appendWindow(bitoscope::host::WindowLease&& lease) {
    return appendWindow(lease);
  }
  std::expected<AppendOutcome, std::string> appendWindow(bitoscope::host::WindowLease& lease,
                                                         ArenaBlockSupply& supply) {
    auto packed = packWindow(lease, scratch_);
    if (!packed) return std::unexpected(packed.error());
    return appendPacked(*packed, &supply);
  }

  // WHY: Pad bits are zeroed in the LEASE's own buffer to save a copy: nothing reads a window after storing.
  std::expected<PackedWindow, std::string> packWindow(bitoscope::host::WindowLease& lease,
                                                      PackScratch& scratch) const {
    if (!lease.valid()) return std::unexpected("window lease is empty");

    const CapturePlan& plan = reservation_.plan;
    const bitoscope::host::WindowInfo& info = lease.info();
    if (auto checked = checkWindowInfo(info); !checked) return std::unexpected(checked.error());
    const uint64_t need = (uint64_t{info.num_samples} + 7) / 8;
    for (uint32_t c = 0; c < plan.channels(); ++c) {
      const std::span<const uint8_t> src = lease.channel(c);
      if (src.size() < need) {
        return std::unexpected(std::format(
            "window buffer holds {} bytes per channel, {} samples need {}", src.size(),
            info.num_samples, need));
      }
    }
    if (scratch.channels_ != plan.channels() || scratch.region_bytes_ != compress_bound_ ||
        scratch.segments_.size() != plan.channels() ||
        (pyramid_ && scratch.bucket_bytes_ < windowBucketBytes(plan.channels(), plan.samplesPerWindow()))) {
      return std::unexpected(std::format(
          "pack scratch was made for {} channels and {}-byte regions, capture has {} "
          "channels and {}-byte regions",
          scratch.channels_, scratch.region_bytes_, plan.channels(), compress_bound_));
    }

    for (uint32_t c = 0; c < plan.channels(); ++c) {
      const std::span<uint8_t> raw = lease.channel(c).first(static_cast<size_t>(need));
      if (const uint32_t used = info.num_samples % 8; used != 0) {
        raw[raw.size() - 1] &= static_cast<uint8_t>((1u << used) - 1u);
      }

      const std::span<uint8_t> room(scratch.packed_.get() + uint64_t{c} * compress_bound_,
                                    static_cast<size_t>(compress_bound_));
      auto compressed = bitoscope::host::lz4Compress(raw, room);
      if (!compressed) return std::unexpected(compressed.error());

      // WHY: Hard bound, apart from the 95% policy: a segment never costs more arena than raw (guaranteedSamples()).
      const bool fits = uint64_t{*compressed} < need;
      const bool worth_it = uint64_t{*compressed} * 100 <= need * uint64_t{kLz4AcceptPercent};
      PackedSegment& segment = scratch.segments_[c];
      if (fits && worth_it) {
        segment.codec = SegmentCodec::Lz4;
        segment.bytes = room.first(*compressed);
      } else {
        segment.codec = SegmentCodec::None;
        segment.bytes = raw;
      }
      segment.payload_crc32c = bitoscope::host::crc32c(segment.bytes);
    }
    WindowBuckets buckets;
    if (pyramid_ && buckets_live_.value.load(std::memory_order_acquire)) {
      const uint32_t stride = maxWindowBuckets(plan.samplesPerWindow());
      for (uint32_t c = 0; c < plan.channels(); ++c) {
        buckets.count = windowBucketBits(info.first_sample_index, info.num_samples,
                                         lease.channel(c).first(static_cast<size_t>(need)),
                                         scratch.buckets().subspan(uint64_t{c} * stride, stride));
      }
      buckets.first_bucket = info.first_sample_index / Pyramid::kBaseBucketSamples;
      buckets.stride = stride;
      buckets.bits = scratch.buckets();
    }
    return PackedWindow{info, std::span<const PackedSegment>(scratch.segments_), buckets};
  }

  std::expected<AppendOutcome, std::string> appendPacked(const PackedWindow& packed,
                                                         ArenaBlockSupply* supply = nullptr) {
    if (finalized_) return std::unexpected("capture buffer is finalized");
    if (mode_ == StoreMode::Armed) {
      return std::unexpected(
          "capture buffer is armed: its windows go through appendArmed() until "
          "settleHistory()");
    }
    const uint64_t stored_end = records_.empty() ? 0 : records_.back().endSample();
    if (auto checked = checkPacked(packed, stored_end); !checked) {
      return std::unexpected(checked.error());
    }
    const bitoscope::host::WindowInfo& info = packed.info;

    const std::optional<Trial> trial = trialPlace(packed);
    const bool past_pyramid = pyramid_ && info.first_sample_index + info.num_samples > pyramid_capacity_;
    if (!trial || records_.size() >= max_windows_ || past_pyramid) {
      overflow_samples_ += info.num_samples;
      return AppendOutcome::Full;
    }
    auto taken = takePlacedBlocks(info.first_sample_index, supply);
    if (!taken) return std::unexpected(taken.error());
    if (!*taken) {
      overflow_samples_ += info.num_samples;
      return AppendOutcome::Full;
    }

    const bool first = records_.empty();
    storePlaced(packed, *trial, records_.size());
    if (first) leading_missing_ = info.first_sample_index;
    stored_samples_ += info.num_samples;
    missing_between_ += info.gap_samples_before;
    return AppendOutcome::Stored;
  }

  std::expected<void, std::string> armHistory(uint64_t retain_samples) {
    if (finalized_) return std::unexpected("capture buffer is finalized");
    if (mode_ != StoreMode::Linear) {
      return std::unexpected(mode_ == StoreMode::Armed
                                 ? "capture buffer is already armed"
                                 : "capture buffer has settled its history; it arms once");
    }
    if (!records_.empty() || overflow_samples_ != 0) {
      return std::unexpected(
          "capture buffer already holds windows; only empty buffer can be armed");
    }
    if (retain_samples == 0) {
      return std::unexpected("armed history must retain at least one sample");
    }
    if (max_windows_ == 0) {
      return std::unexpected("capture buffer has no index to arm");
    }
    mode_ = StoreMode::Armed;
    buckets_live_.value.store(false, std::memory_order_release);
    published_.store(kNotReadable);
    retain_samples_ = retain_samples;
    rec_head_ = 0;
    rec_count_ = 0;
    armed_end_ = 0;
    head_ = cursor_;
    wrapped_ = false;
    return {};
  }

  // WHY: Retire old windows BEFORE the trial placement: ring floor (invariant I7) counts the room they free.
  std::expected<AppendOutcome, std::string> appendArmed(const PackedWindow& packed) {
    if (finalized_) return std::unexpected("capture buffer is finalized");
    if (mode_ != StoreMode::Armed) {
      return std::unexpected(mode_ == StoreMode::Linear
                                 ? "capture buffer is not armed: armHistory() first"
                                 : "capture buffer has settled its history: later windows go "
                                   "through appendPacked()");
    }
    if (auto checked = checkPacked(packed, armed_end_); !checked) {
      return std::unexpected(checked.error());
    }
    const bitoscope::host::WindowInfo& info = packed.info;
    const uint64_t end = info.first_sample_index + info.num_samples;
    retireHistory(info.first_sample_index, end);
    const std::optional<Trial> trial = trialPlace(packed);
    if (!trial || rec_count_ >= max_windows_) return AppendOutcome::NoRoomWhileArmed;
    if (auto taken = takePlacedBlocks(info.first_sample_index, nullptr); !taken) return std::unexpected(taken.error());
    storePlaced(packed, *trial, (rec_head_ + rec_count_) % max_windows_);
    ++rec_count_;
    stored_samples_ += info.num_samples;
    armed_end_ = end;
    return AppendOutcome::Stored;
  }

  std::expected<AppendOutcome, std::string> appendWindowArmed(bitoscope::host::WindowLease& lease) {
    auto packed = packWindow(lease, scratch_);
    if (!packed) return std::unexpected(packed.error());
    return appendArmed(*packed);
  }

  LiveHistory liveHistory() const {
    const uint64_t windows = windowCount();
    if (windows == 0) return {};
    return {record(0).first_sample_index, record(windows - 1).endSample(), windows};
  }

  std::expected<uint64_t, std::string> settleHistory(uint64_t requested_begin) {
    if (finalized_) return std::unexpected("capture buffer is finalized");
    if (mode_ != StoreMode::Armed) {
      return std::unexpected(mode_ == StoreMode::Linear
                                 ? "capture buffer is not armed: it has no history to settle"
                                 : "capture buffer has already settled its history");
    }
    if (rec_count_ == 0) return std::unexpected("armed capture buffer holds no history");
    while (rec_count_ > 1 && record(0).endSample() <= requested_begin) evictOldest();
    const uint64_t origin = record(0).first_sample_index;
    origin_ = origin;
    flattenIndex();
    stored_samples_ = 0;
    missing_between_ = 0;
    for (IndexRecord& r : records_) {
      r.first_sample_index -= origin;
      stored_samples_ += r.num_samples;
      missing_between_ += r.gap_samples_before;
    }
    leading_missing_ = records_.front().first_sample_index;
    head_ = segments_.front().storage_offset;
    wrapped_ = head_ >= cursor_;
    rec_head_ = 0;
    rec_count_ = 0;
    mode_ = StoreMode::Settled;
    history_windows_ = records_.size();
    history_end_.value.store(records_.back().endSample(), std::memory_order_relaxed);
    if (pyramid_) {
      history_.value.store(PyramidHistory::Pending, std::memory_order_release);
      buckets_live_.value.store(true, std::memory_order_release);
    }
    published_.store(records_.size());
    return origin;
  }

  StoreMode storeMode() const { return mode_; }

  uint64_t origin() const { return origin_; }

  std::string describeSample(uint64_t store_sample) const {
    if (origin_ == 0) return std::to_string(store_sample);
    return std::format("{} (sample {} of capture)", store_sample + origin_, store_sample);
  }

  std::expected<void, std::string> read(uint32_t channel, uint64_t first_byte,
                                        std::span<uint8_t> out) const {
    const std::span<const IndexRecord> records = publishedRecords();
    if (published_.load(std::memory_order_relaxed) == kNotReadable) {
      return std::unexpected(
          "capture buffer is armed: its history is read once settleHistory() has made it "
          "capture");
    }
    const CapturePlan& plan = reservation_.plan;
    if (channel >= plan.channels()) {
      return std::unexpected(std::format("channel {} does not exist, capture has {}", channel,
                                         plan.channels()));
    }
    if (out.empty()) return {};
    if (first_byte > (std::numeric_limits<uint64_t>::max)() / 8) {
      return std::unexpected(std::format("byte {} is beyond any sample index", first_byte));
    }

    uint64_t byte = first_byte;
    const auto after = std::upper_bound(records.begin(), records.end(), byte * 8,
                                        [](uint64_t sample, const IndexRecord& r) {
                                          return sample < r.first_sample_index;
                                        });
    const IndexRecord* rec = after == records.begin() ? nullptr : &*(after - 1);
    size_t done = 0;
    while (done < out.size()) {
      const uint64_t sample = byte * 8;
      if (rec == nullptr || sample < rec->first_sample_index || sample >= rec->endSample()) {
        return std::unexpected(std::format("sample {} is not stored", describeSample(sample)));
      }
      if (rec->first_sample_index % 8 != 0) {
        return std::unexpected(std::format(
            "window at sample {} does not start on byte boundary", rec->first_sample_index));
      }
      const size_t window = static_cast<size_t>(rec - records.data());
      const uint64_t in_window = byte - rec->first_sample_index / 8;
      const uint64_t window_bytes = (uint64_t{rec->num_samples} + 7) / 8;
      const uint64_t n = (std::min)(window_bytes - in_window, uint64_t{out.size() - done});
      const bool partial_last = rec->num_samples % 8 != 0;
      if (partial_last && window + 1 < records.size() && in_window + n == window_bytes) {
        return std::unexpected(std::format(
            "byte {} straddles end of window at sample {} and window after it",
            byte + n - 1, rec->first_sample_index));
      }

      auto decoded = decodeSegment(window, channel, window_bytes);
      if (!decoded) return std::unexpected(decoded.error());
      std::memcpy(out.data() + done, decoded->data() + in_window, static_cast<size_t>(n));
      done += static_cast<size_t>(n);
      byte += n;
      rec = window + 1 < records.size() ? &records[window + 1] : nullptr;
    }
    return {};
  }

  std::expected<bool, std::string> readWholeWindow(uint32_t channel, uint64_t first_sample,
                                                   std::span<uint8_t> out) const {
    const std::span<const IndexRecord> records = publishedRecords();
    if (channel >= reservation_.plan.channels()) {
      return std::unexpected(std::format("channel {} does not exist, capture has {}", channel,
                                         reservation_.plan.channels()));
    }
    const auto at = std::partition_point(records.begin(), records.end(), [&](const IndexRecord& r) {
      return r.first_sample_index < first_sample;
    });
    if (at == records.end() || at->first_sample_index != first_sample) return false;
    if (out.size() != (uint64_t{at->num_samples} + 7) / 8) return false;
    if (auto decoded = decodeSegmentInto(static_cast<size_t>(at - records.begin()), channel, out); !decoded) {
      return std::unexpected(decoded.error());
    }
    return true;
  }

  SegmentCodec segmentCodec(uint64_t window_index, uint32_t channel) const {
    return segment(window_index, channel).codec;
  }
  uint32_t segmentStoredBytes(uint64_t window_index, uint32_t channel) const {
    return segment(window_index, channel).stored_bytes;
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

  // WHY: Arena bytes are not cleared (too costly); stale bytes stay unreachable as every read goes through records_.
  std::expected<Reservation, std::string> releaseReservation() {
    if (!finalized_) return std::unexpected("capture buffer is not finalized");
    if (released_) return std::unexpected("capture buffer's memory was already released");
    if (blocks_released_) {
      return std::unexpected("capture buffer's unused memory was given back; new capture needs new reservation");
    }
    released_ = true;
    published_.store(0);
    records_.clear();
    segments_.clear();
    return std::move(reservation_);
  }

  std::span<const IndexRecord> indexRange(uint64_t first, uint64_t end) const {
    const std::span<const IndexRecord> records = publishedRecords();
    if (first >= end) return {};
    const auto lo = std::partition_point(records.begin(), records.end(),
                                         [&](const IndexRecord& r) { return r.endSample() <= first; });
    const auto hi = std::partition_point(lo, records.end(), [&](const IndexRecord& r) {
      return r.first_sample_index < end;
    });
    return records.subspan(static_cast<size_t>(lo - records.begin()), static_cast<size_t>(hi - lo));
  }

  uint64_t windowCount() const {
    return mode_ == StoreMode::Armed ? rec_count_ : uint64_t{records_.size()};
  }
  uint64_t sampleCount() const {
    const uint64_t windows = windowCount();
    return windows == 0 ? 0 : record(windows - 1).endSample();
  }
  uint64_t storedSamples() const { return stored_samples_; }
  uint64_t missingSamples() const { return leading_missing_ + missing_between_; }
  uint64_t leadingMissingSamples() const { return leading_missing_; }
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

  std::expected<void, std::string> summarizeHistory(const std::atomic<bool>& stop) {
    if (!pyramid_ || history_.value.load(std::memory_order_acquire) != PyramidHistory::Pending) {
      return std::unexpected("this capture has no settled history waiting to be summarized");
    }
    const CapturePlan& plan = reservation_.plan;
    const uint32_t stride = maxWindowBuckets(plan.samplesPerWindow());
    for (uint64_t w = 0; w < history_windows_; ++w) {
      if (stop.load(std::memory_order_relaxed)) return failHistorySummary("capture ended before it was done");
      const IndexRecord& rec = live_records_[w];
      const uint64_t bytes = (uint64_t{rec.num_samples} + 7) / 8;
      const std::span<uint8_t> raw = std::span<uint8_t>(history_raw_).first(static_cast<size_t>(bytes));
      WindowBuckets buckets;
      for (uint32_t c = 0; c < plan.channels(); ++c) {
        if (auto decoded = decodeSegmentInto(static_cast<size_t>(w), c, raw); !decoded) {
          return failHistorySummary(decoded.error());
        }
        buckets.count = windowBucketBits(rec.first_sample_index, rec.num_samples, raw,
                                         std::span<uint8_t>(history_buckets_).subspan(uint64_t{c} * stride, stride));
      }
      buckets.first_bucket = rec.first_sample_index / Pyramid::kBaseBucketSamples;
      buckets.stride = stride;
      buckets.bits = history_buckets_;
      constexpr bool concurrentWriters = true;
      pyramid_->fold(buckets, concurrentWriters);
    }
    history_.value.store(PyramidHistory::Ready, std::memory_order_release);
    return {};
  }

  std::unexpected<std::string> failHistorySummary(std::string why) {
    history_error_ = why;
    history_.value.store(PyramidHistory::Failed, std::memory_order_release);
    return std::unexpected(std::move(why));
  }

  std::expected<uint64_t, std::string> releaseUnusedBlocks() {
    if (!finalized_) return std::unexpected("capture buffer is not finalized");
    if (released_) return std::unexpected("capture buffer's memory was already released");
    if (mode_ == StoreMode::Armed) return std::unexpected("armed history's memory is not given back");
    const uint64_t block = reservation_.block_bytes;
    std::vector<bool> holds_segment(reservation_.blocks.size(), false);
    for (const ChannelSegment& seg : segments_) holds_segment[static_cast<size_t>(seg.storage_offset / block)] = true;
    uint64_t freed = 0;
    for (uint64_t i = 0; i < reservation_.blocks.size(); ++i) {
      if (holds_segment[static_cast<size_t>(i)] || !reservation_.blocks[static_cast<size_t>(i)]) continue;
      freed += reservation_.blockSize(i);
      reservation_.blocks[static_cast<size_t>(i)].reset();
    }
    blocks_released_ = true;
    return freed;
  }
  bool blocksReleased() const { return blocks_released_; }
  uint64_t pyramidBytes() const { return pyramid_ ? pyramid_->memoryBytes() : 0; }
  uint8_t pyramidPlaneByteForTest(uint32_t channel, size_t level, uint64_t byte, bool ones) const {
    return pyramid_ ? pyramid_->planeByteForTest(channel, level, byte, ones) : uint8_t{0};
  }
  size_t pyramidLevelsForTest() const { return pyramid_ ? pyramid_->levelCount() : 0; }
  std::expected<void, std::string> pyramidSummary(uint32_t channel, uint64_t begin, uint64_t end,
                                                  std::span<Bucket> out) const {
    if (!pyramid_) return std::unexpected("this capture builds no pyramid");
    const uint64_t available = readableSampleCount();
    if (begin < history_end_.value.load(std::memory_order_relaxed)) {
      switch (history_.value.load(std::memory_order_acquire)) {
        case PyramidHistory::Pending:
          return std::unexpected("history before trigger is still being summarized");
        case PyramidHistory::Failed:
          return std::unexpected("history before trigger could not be summarized: " + history_error_);
        case PyramidHistory::None:
        case PyramidHistory::Ready:
          break;
      }
    }
    return pyramid_->summary(channel, begin, end, available, out);
  }

  std::optional<Bucket> pyramidRangeExact(uint32_t channel, uint64_t begin, uint64_t end) const {
    if (!pyramid_) return std::nullopt;
    if (begin < history_end_.value.load(std::memory_order_relaxed) &&
        history_.value.load(std::memory_order_acquire) != PyramidHistory::Ready &&
        history_.value.load(std::memory_order_acquire) != PyramidHistory::None) {
      return std::nullopt;
    }
    return pyramid_->exactRange(channel, begin, end, readableSampleCount());
  }

  uint64_t arenaBytes() const { return reservation_.arenaBytes(); }
  uint64_t arenaUsedBytes() const {
    return wrapped_ ? reservation_.arenaBytes() - head_ + cursor_ : cursor_ - head_;
  }

  uint64_t indexReservedBytesForTest() const {
    return uint64_t{records_.capacity()} * sizeof(IndexRecord) +
           uint64_t{segments_.capacity()} * sizeof(ChannelSegment);
  }

  uint64_t arenaBytesForTest() const { return reservation_.arenaBytes(); }

  uint64_t segmentDecodeCallsForTest() const {
    return segment_decode_calls_.value.load(std::memory_order_relaxed);
  }

 private:
  RawCaptureBuffer() = default;

  std::expected<std::vector<uint8_t>, std::string> decodeSegment(size_t window_index,
                                                                  uint32_t channel,
                                                                  uint64_t window_bytes) const {
    std::vector<uint8_t> decoded(static_cast<size_t>(window_bytes));
    if (auto r = decodeSegmentInto(window_index, channel, decoded); !r) return std::unexpected(r.error());
    return decoded;
  }

  std::expected<void, std::string> decodeSegmentInto(size_t window_index, uint32_t channel,
                                                     std::span<uint8_t> decoded) const {
    segment_decode_calls_.value.fetch_add(1, std::memory_order_relaxed);
    const CapturePlan& plan = reservation_.plan;
    const ChannelSegment& seg = live_segments_[window_index * plan.channels() + channel];
    const std::span<const uint8_t> stored = reservation_.at(seg.storage_offset,
                                                            seg.stored_bytes);
    if (stored.size() != seg.stored_bytes) {
      return std::unexpected(std::format(
          "segment for channel {} at sample {} is recorded at arena offset {}, which is not "
          "in arena", channel, describeSample(live_records_[window_index].first_sample_index),
          seg.storage_offset));
    }
    if (bitoscope::host::crc32c(stored) != seg.payload_crc32c) {
      return std::unexpected(std::format(
          "segment for channel {} at sample {} failed its checksum", channel,
          describeSample(live_records_[window_index].first_sample_index)));
    }
    if (seg.codec == SegmentCodec::Lz4) {
      auto r = bitoscope::host::lz4Decompress(stored, decoded);
      if (!r) {
        return std::unexpected(std::format(
            "segment for channel {} at sample {} failed to decompress: {}", channel,
            describeSample(live_records_[window_index].first_sample_index), r.error()));
      }
    } else {
      std::memcpy(decoded.data(), stored.data(), stored.size());
    }
    return {};
  }

  // WHY: Try the cursor BEFORE the arena's front: front first abandons room the ring floor counts on.
  std::optional<uint64_t> ringPlace(uint64_t at, uint64_t bytes, bool& wrapped) const {
    if (!wrapped) {
      if (const auto here = reservation_.place(at, bytes)) return here;
      const auto front = reservation_.place(0, bytes);
      if (!front || *front + bytes > head_) return std::nullopt;
      wrapped = true;
      return front;
    }
    const auto here = reservation_.place(at, bytes);
    if (!here || *here + bytes > head_) return std::nullopt;
    return here;
  }

  friend struct RingPlaceTestAccess;

  struct Trial {
    uint64_t cursor = 0;
    bool wrapped = false;
  };

  std::optional<Trial> trialPlace(const PackedWindow& packed) {
    Trial trial{cursor_, wrapped_};
    for (uint32_t c = 0; c < reservation_.plan.channels(); ++c) {
      const uint64_t bytes = packed.segments[c].bytes.size();
      const auto at = ringPlace(trial.cursor, bytes, trial.wrapped);
      if (!at) return std::nullopt;
      placed_[c] = *at;
      trial.cursor = *at + bytes;
    }
    return trial;
  }

  std::expected<bool, std::string> takePlacedBlocks(uint64_t first_sample, ArenaBlockSupply* supply) {
    for (uint32_t c = 0; c < reservation_.plan.channels(); ++c) {
      const uint64_t index = placed_[c] / reservation_.block_bytes;
      std::shared_ptr<uint8_t[]>& block = reservation_.blocks[static_cast<size_t>(index)];
      if (block) continue;
      if (supply == nullptr) {
        return std::unexpected(std::format("window at sample {} is placed in arena block {}, which holds no memory",
                                           describeSample(first_sample), index));
      }
      auto supplied = supply->supply(index, reservation_.blockSize(index));
      if (!supplied || *supplied == nullptr) return false;
      block = std::move(*supplied);
    }
    return true;
  }

  void storePlaced(const PackedWindow& packed, const Trial& trial, uint64_t slot) {
    const uint32_t channels = reservation_.plan.channels();
    const bool append = slot == records_.size();
    const size_t base = static_cast<size_t>(slot) * channels;
    for (uint32_t c = 0; c < channels; ++c) {
      const PackedSegment& segment = packed.segments[c];
      const std::span<uint8_t> dst = reservation_.at(placed_[c], segment.bytes.size());
      std::memcpy(dst.data(), segment.bytes.data(), dst.size());
      const ChannelSegment stored{segment.codec, placed_[c],
                                  static_cast<uint32_t>(segment.bytes.size()),
                                  segment.payload_crc32c};
      if (append) {
        segments_.push_back(stored);
      } else {
        segments_[base + c] = stored;
      }
    }
    cursor_ = trial.cursor;
    wrapped_ = trial.wrapped;
    const bitoscope::host::WindowInfo& info = packed.info;
    const IndexRecord record{info.first_sample_index, info.num_samples, info.gap_samples_before};
    if (append) {
      records_.push_back(record);
    } else {
      records_[static_cast<size_t>(slot)] = record;
    }
    if (pyramid_ && mode_ != StoreMode::Armed) {
      pyramid_->fold(packed.buckets, history_.value.load(std::memory_order_acquire) == PyramidHistory::Pending);
    }
    if (mode_ != StoreMode::Armed) published_.store(records_.size());
  }

  size_t physical(uint64_t window) const {
    return static_cast<size_t>(mode_ == StoreMode::Armed ? (rec_head_ + window) % max_windows_
                                                         : window);
  }
  const IndexRecord& record(uint64_t window) const { return records_[physical(window)]; }
  const ChannelSegment& segment(uint64_t window, uint32_t channel) const {
    return segments_[physical(window) * reservation_.plan.channels() + channel];
  }

  void retireHistory(uint64_t first, uint64_t end) {
    while (rec_count_ != 0) {
      const uint64_t second = rec_count_ >= 2 ? record(1).first_sample_index : first;
      if (end < retain_samples_ || second > end - retain_samples_) return;
      evictOldest();
    }
  }

  void evictOldest() {
    stored_samples_ -= records_[static_cast<size_t>(rec_head_)].num_samples;
    rec_head_ = (rec_head_ + 1) % max_windows_;
    --rec_count_;
    head_ = rec_count_ == 0 ? cursor_ : segment(0, 0).storage_offset;
    wrapped_ = rec_count_ != 0 && head_ >= cursor_;
  }

  void flattenIndex() {
    const uint32_t channels = reservation_.plan.channels();
    flattenRing(records_, static_cast<size_t>(rec_head_), static_cast<size_t>(rec_count_));
    flattenRing(segments_, static_cast<size_t>(rec_head_) * channels,
                static_cast<size_t>(rec_count_) * channels);
  }

  template <typename T>
  static void flattenRing(std::vector<T>& ring, size_t head, size_t count) {
    const size_t size = ring.size();
    if (head + count <= size) {
      if (head != 0) std::move(ring.begin() + head, ring.begin() + head + count, ring.begin());
    } else {
      const size_t newer = head + count - size;
      const size_t from = head - newer;
      if (from != 0) std::move_backward(ring.begin(), ring.begin() + newer, ring.begin() + head);
      std::rotate(ring.begin() + from, ring.begin() + head, ring.end());
      if (from != 0) std::move(ring.begin() + from, ring.end(), ring.begin());
    }
    ring.resize(count);
  }

  std::expected<void, std::string> checkPacked(const PackedWindow& packed,
                                               uint64_t stored_end) const {
    const CapturePlan& plan = reservation_.plan;
    if (pyramid_ && mode_ != StoreMode::Armed && packed.buckets.count == 0) {
      return std::unexpected("capture builds pyramid and packed window carries no buckets");
    }
    const bitoscope::host::WindowInfo& info = packed.info;
    if (packed.segments.size() != plan.channels()) {
      return std::unexpected(std::format("packed window has {} segments, capture has {} "
                                         "channels",
                                         packed.segments.size(), plan.channels()));
    }
    if (auto checked = checkWindowInfo(info); !checked) return std::unexpected(checked.error());
    if (info.first_sample_index < stored_end) {
      return std::unexpected(std::format(
          "window at sample {} would overlap stored samples, which end at sample {}",
          describeSample(info.first_sample_index), describeSample(stored_end)));
    }
    const uint64_t need = (uint64_t{info.num_samples} + 7) / 8;
    for (uint32_t c = 0; c < plan.channels(); ++c) {
      const PackedSegment& segment = packed.segments[c];
      const bool size_ok = segment.codec == SegmentCodec::Lz4 ? segment.bytes.size() < need
                                                               : segment.bytes.size() == need;
      if (!size_ok) {
        return std::unexpected(std::format(
            "packed segment for channel {} holds {} bytes, which window of {} samples cannot "
            "have stored",
            c, segment.bytes.size(), info.num_samples));
      }
    }
    return {};
  }

  std::expected<void, std::string> checkWindowInfo(const bitoscope::host::WindowInfo& info) const {
    const CapturePlan& plan = reservation_.plan;
    if (info.num_channels != plan.channels()) {
      return std::unexpected(std::format("window has {} channels, capture has {}",
                                         info.num_channels, plan.channels()));
    }
    if (info.sample_rate_milli_hz != plan.sampleRateMilliHz()) {
      return std::unexpected(std::format("window sample rate {} mHz differs from capture's {}",
                                         info.sample_rate_milli_hz, plan.sampleRateMilliHz()));
    }
    if (info.num_samples == 0 || info.num_samples > plan.samplesPerWindow()) {
      return std::unexpected(std::format("window has {} samples, expected 1 to {}",
                                         info.num_samples, plan.samplesPerWindow()));
    }
    return {};
  }

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
  uint64_t leading_missing_ = 0;
  uint64_t origin_ = 0;
  uint64_t missing_between_ = 0;
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
