/*
 * SPDX-License-Identifier: MIT
 */

#include "ring_feed.h"

#include <algorithm>
#include <array>
#include <new>
#include <optional>
#include <span>
#include <utility>

#include "pyramid.h"
#include "raw_buffer_signal_view.h"

namespace bitoscope::decoder_host {

namespace {

using BeforeAllocate = std::function<void(AllocationSite, uint64_t)>;
constexpr uint64_t kBucket = capture::Pyramid::kBaseBucketSamples;

std::unexpected<FeedError> feedError(FeedFailure kind, std::string message) {
  return std::unexpected(FeedError{kind, std::move(message)});
}

std::unexpected<FeedError> fromRing(const host::DecodeRingError& e) {
  return feedError(e.kind == host::DecodeRingErrorKind::ConsumerClosed ? FeedFailure::ConsumerClosed : FeedFailure::Failed,
                   e.message);
}

// WHY: A timed-out slice has delivered what it could (nextSample() says how much); the rest is written again.
std::expected<void, FeedError> writePlanes(host::DecodeRingProducer& ring, std::span<const std::span<const uint8_t>> planes,
                                           uint64_t first_bit, uint64_t n, const StopFlag& stop) {
  uint64_t done = 0;
  while (done < n) {
    if (stop.load(std::memory_order_relaxed)) return feedError(FeedFailure::Stopped, "stopped");
    const uint64_t before = ring.nextSample();
    auto written = ring.writePlanes(planes, first_bit + done, n - done, ring.ring().nowMs() + kFeedSliceMs);
    done += ring.nextSample() - before;
    if (!written && written.error().kind != host::DecodeRingErrorKind::Timeout) return fromRing(written.error());
  }
  return {};
}

std::expected<void, FeedError> writeSkip(host::DecodeRingProducer& ring, uint32_t levels, uint64_t n, const StopFlag& stop) {
  const std::array<uint8_t, 4> bytes{static_cast<uint8_t>(levels), static_cast<uint8_t>(levels >> 8),
                                     static_cast<uint8_t>(levels >> 16), static_cast<uint8_t>(levels >> 24)};
  for (;;) {
    if (stop.load(std::memory_order_relaxed)) return feedError(FeedFailure::Stopped, "stopped");
    const uint64_t before = ring.nextSample();
    auto written = ring.writeSkip(bytes, n, ring.ring().nowMs() + kFeedSliceMs);
    if (written) return {};
    if (written.error().kind != host::DecodeRingErrorKind::Timeout) return fromRing(written.error());
    n -= ring.nextSample() - before;
  }
}

class FlatBuckets {
 public:
  static constexpr uint64_t kBatch = 4096;

  FlatBuckets(const capture::RawCaptureBuffer& buffer, const std::vector<uint32_t>& channels, uint64_t first, uint64_t end,
              uint64_t min_buckets)
      : buffer_(buffer), channels_(channels), first_(first), end_(end), min_buckets_(min_buckets) {
    usable_ = min_buckets_ > 0 && buffer.hasPyramid() && first_ < end_;
  }

  void allocate() {
    if (!usable_) return;
    columns_.resize(static_cast<size_t>(kBatch));
    cache_.resize(static_cast<size_t>(kBatch));
  }

  bool usable() const { return usable_; }
  uint64_t minBuckets() const { return min_buckets_; }

  std::optional<uint32_t> levels(uint64_t b) {
    if (!usable_ || b < first_ || b >= end_) return std::nullopt;
    if (b < cache_first_ || b >= cache_first_ + cache_count_) load(b);
    const int64_t v = cache_[static_cast<size_t>(b - cache_first_)];
    if (v < 0) return std::nullopt;
    return static_cast<uint32_t>(v);
  }

  uint64_t runFrom(uint64_t b) {
    if (b == last_run_first_) return last_run_length_;
    uint64_t n = 0;
    if (const auto l = levels(b)) {
      n = 1;
      while (levels(b + n) == l) ++n;
    }
    last_run_first_ = b;
    last_run_length_ = n;
    return n;
  }

  std::optional<uint64_t> nextRunStart(uint64_t from, uint64_t limit) {
    uint64_t b = (std::max)(from, first_);
    while (b < limit && b < end_) {
      const uint64_t n = runFrom(b);
      if (n >= min_buckets_) return b;
      b += n == 0 ? 1 : n;
    }
    return std::nullopt;
  }

 private:
  void load(uint64_t b) {
    cache_first_ = b;
    cache_count_ = (std::min)(kBatch, end_ - b);
    const std::span<capture::Bucket> columns(columns_.data(), static_cast<size_t>(cache_count_));
    std::fill(cache_.begin(), cache_.begin() + static_cast<ptrdiff_t>(cache_count_), int64_t{0});
    for (size_t i = 0; i < channels_.size(); ++i) {
      if (!buffer_.pyramidSummary(channels_[i], b * kBucket, (b + cache_count_) * kBucket, columns)) {
        std::fill(cache_.begin(), cache_.begin() + static_cast<ptrdiff_t>(cache_count_), int64_t{-1});
        return;
      }
      for (uint64_t k = 0; k < cache_count_; ++k) {
        int64_t& v = cache_[static_cast<size_t>(k)];
        if (v < 0) continue;
        const capture::Bucket& column = columns[static_cast<size_t>(k)];
        if (column.has_zero == column.has_one) {
          v = -1;
        } else if (column.has_one) {
          v |= int64_t{1} << i;
        }
      }
    }
  }

  const capture::RawCaptureBuffer& buffer_;
  const std::vector<uint32_t>& channels_;
  uint64_t first_;
  uint64_t end_;
  uint64_t min_buckets_;
  bool usable_ = false;
  std::vector<capture::Bucket> columns_;
  std::vector<int64_t> cache_;
  uint64_t cache_first_ = 0;
  uint64_t cache_count_ = 0;
  uint64_t last_run_first_ = UINT64_MAX;
  uint64_t last_run_length_ = 0;
};

std::expected<void, FeedError> feedCapture(const capture::RawCaptureBuffer& buffer, capture::CaptureExtent extent,
                                           const std::vector<uint32_t>& channels, const CaptureFeedOptions& options,
                                           host::DecodeRingProducer& ring, const StopFlag& stop) {
  if (options.before_feed) options.before_feed(stop);
  if (stop.load(std::memory_order_relaxed)) return feedError(FeedFailure::Stopped, "stopped");
  const auto bound = static_cast<uint32_t>(channels.size());
  const uint64_t window_bytes = (uint64_t{buffer.plan().samplesPerWindow()} + 7) / 8;
  const uint64_t first = extent.first_sample;
  const uint64_t end = extent.first_sample + extent.count;
  FlatBuckets flats(buffer, channels, (first + kBucket - 1) / kBucket, end / kBucket,
                    (options.min_skip_samples + kBucket - 1) / kBucket);

  std::vector<std::vector<uint8_t>> planes;
  std::vector<std::span<const uint8_t>> spans;
  try {
    const uint64_t bytes = window_bytes * bound;
    if (options.before_allocate) options.before_allocate(AllocationSite::FeedPlanes, bytes);
    planes.assign(bound, std::vector<uint8_t>(static_cast<size_t>(window_bytes)));
    for (const auto& p : planes) spans.emplace_back(p);
    flats.allocate();
  } catch (const std::bad_alloc&) {
    return feedError(FeedFailure::OutOfMemory, "out of memory for samples of " + std::to_string(bound) +
                                                   " bound channels (" + std::to_string(window_bytes * bound) +
                                                   " bytes window)");
  }

  uint64_t loaded = UINT64_MAX;
  uint64_t pos = first;
  while (pos < end) {
    if (stop.load(std::memory_order_relaxed)) return feedError(FeedFailure::Stopped, "stopped");
    if (flats.usable() && pos % kBucket == 0) {
      const uint64_t b = pos / kBucket;
      const uint64_t n = flats.runFrom(b);
      if (n != 0 && n >= flats.minBuckets()) {
        if (auto skipped = writeSkip(ring, *flats.levels(b), n * kBucket, stop); !skipped) return skipped;
        pos += n * kBucket;
        continue;
      }
    }
    const std::span<const capture::IndexRecord> records = buffer.indexRange(pos, pos + 1);
    if (records.empty()) return feedError(FeedFailure::Failed, "sample " + std::to_string(pos) + " of capture is not stored");
    const capture::IndexRecord& window = records.front();
    uint64_t piece_end = (std::min)(window.endSample(), end);
    if (flats.usable()) {
      if (const auto next = flats.nextRunStart((pos + kBucket - 1) / kBucket, (piece_end + kBucket - 1) / kBucket)) {
        piece_end = (std::min)(piece_end, *next * kBucket);
      }
    }
    if (loaded != window.first_sample_index) {
      const uint64_t bytes = (uint64_t{window.num_samples} + 7) / 8;
      for (uint32_t c = 0; c < bound; ++c) {
        auto read = buffer.read(channels[c], window.first_sample_index / 8,
                                std::span<uint8_t>(planes[c]).first(static_cast<size_t>(bytes)));
        if (!read) {
          return feedError(FeedFailure::Failed, "capture could not be read: " + read.error());
        }
      }
      loaded = window.first_sample_index;
    }
    if (auto written = writePlanes(ring, spans, pos - window.first_sample_index, piece_end - pos, stop); !written) {
      return written;
    }
    pos = piece_end;
  }
  return {};
}

void setBits(std::vector<uint8_t>& plane, uint64_t from, uint64_t to) {
  for (uint64_t i = from; i < to;) {
    if (i % 8 == 0 && to - i >= 8) {
      const uint64_t whole = (to - i) / 8;
      std::fill_n(plane.begin() + static_cast<ptrdiff_t>(i / 8), static_cast<size_t>(whole), uint8_t{0xFF});
      i += whole * 8;
      continue;
    }
    plane[static_cast<size_t>(i / 8)] = static_cast<uint8_t>(plane[static_cast<size_t>(i / 8)] | (1u << (i % 8)));
    ++i;
  }
}

}

std::expected<FeedFunction, std::string> captureFeed(const capture::RawCaptureBuffer& buffer, capture::CaptureExtent extent,
                                                     capture::ExtentTail tail, std::vector<uint32_t> channels,
                                                     CaptureFeedOptions options) {
  if (auto view = capture::RawBufferSignalView::create(buffer, extent, tail); !view) {
    return std::unexpected(std::move(view.error()));
  }
  if (channels.empty()) return std::unexpected(std::string("decode job needs at least one bound channel"));
  if (channels.size() > decode::kMaxChannels) {
    return std::unexpected("decode job binds at most " + std::to_string(decode::kMaxChannels) + " channels; " +
                           std::to_string(channels.size()) + " were bound");
  }
  for (const uint32_t c : channels) {
    if (c >= buffer.plan().channels()) {
      return std::unexpected("channel " + std::to_string(c) + " is bound, but capture has " +
                             std::to_string(buffer.plan().channels()) + " channels");
    }
  }
  return FeedFunction([&buffer, extent, channels = std::move(channels), options = std::move(options)](
                          host::DecodeRingProducer& ring, const StopFlag& stop) {
    return feedCapture(buffer, extent, channels, options, ring, stop);
  });
}

FeedFunction viewFeed(const decode::SignalView& view, std::vector<uint32_t> channels, BeforeAllocate before_allocate) {
  return [&view, channels = std::move(channels), before_allocate = std::move(before_allocate)](
             host::DecodeRingProducer& ring, const StopFlag& stop) -> std::expected<void, FeedError> {
    constexpr uint64_t kChunk = 64 * 1024;
    const auto bound = static_cast<uint32_t>(channels.size());
    const uint64_t count = view.sampleCount();
    std::vector<std::vector<uint8_t>> planes;
    std::vector<std::span<const uint8_t>> spans;
    std::vector<uint64_t> next_edge(bound, 0);
    try {
      if (before_allocate) before_allocate(AllocationSite::FeedPlanes, kChunk / 8 * bound);
      planes.assign(bound, std::vector<uint8_t>(static_cast<size_t>(kChunk / 8)));
      for (const auto& p : planes) spans.emplace_back(p);
    } catch (const std::bad_alloc&) {
      return feedError(FeedFailure::OutOfMemory, "out of memory for samples of " + std::to_string(bound) +
                                                     " bound channels (" + std::to_string(kChunk / 8 * bound) +
                                                     " bytes piece)");
    }
    for (uint32_t i = 0; i < bound; ++i) next_edge[i] = view.nextEdge(channels[i], 0, decode::EdgeKind::Any).value_or(count);
    for (uint64_t pos = 0; pos < count;) {
      const uint64_t n = (std::min)(kChunk, count - pos);
      for (uint32_t i = 0; i < bound; ++i) {
        std::fill(planes[i].begin(), planes[i].end(), uint8_t{0});
        uint64_t s = pos;
        while (s < pos + n) {
          while (next_edge[i] <= s) next_edge[i] = view.nextEdge(channels[i], next_edge[i], decode::EdgeKind::Any).value_or(count);
          const uint64_t to = (std::min)(next_edge[i], pos + n);
          if (view.level(channels[i], s)) setBits(planes[i], s - pos, to - pos);
          s = to;
        }
      }
      // WHY: A view that could not read part of its capture answered none there: those planes are not samples; never feed.
      if (const std::string unread = view.readError(); !unread.empty()) return feedError(FeedFailure::Failed, unread);
      if (auto written = writePlanes(ring, spans, 0, n, stop); !written) return written;
      pos += n;
    }
    return {};
  };
}

}
