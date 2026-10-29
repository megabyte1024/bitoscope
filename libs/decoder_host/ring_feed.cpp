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

class StoredCapture final : public GrowingCapture {
 public:
  StoredCapture(const capture::RawCaptureBuffer& buffer, capture::CaptureExtent extent)
      : buffer_(buffer), extent_(extent) {}

  const capture::CapturePlan& plan() const override { return buffer_.plan(); }
  bool hasPyramid() const override { return buffer_.hasPyramid(); }
  capture::CaptureExtent publishedExtent() const override { return extent_; }
  std::span<const capture::IndexRecord> indexRange(uint64_t first, uint64_t end) const override {
    return buffer_.indexRange(first, end);
  }
  std::expected<void, std::string> read(uint32_t channel, uint64_t first_byte, std::span<uint8_t> out) const override {
    return buffer_.read(channel, first_byte, out);
  }
  std::expected<void, std::string> pyramidSummary(uint32_t channel, uint64_t begin, uint64_t end,
                                                  std::span<capture::Bucket> out) const override {
    return buffer_.pyramidSummary(channel, begin, end, out);
  }

 private:
  const capture::RawCaptureBuffer& buffer_;
  capture::CaptureExtent extent_;
};

class FlatBuckets {
 public:
  static constexpr uint64_t kBatch = 4096;

  FlatBuckets(const GrowingCapture& capture, const std::vector<uint32_t>& channels, uint64_t first, uint64_t min_buckets)
      : capture_(capture), channels_(channels), first_(first), end_(first), min_buckets_(min_buckets) {
    usable_ = min_buckets_ > 0 && capture.hasPyramid();
  }

  void allocate() {
    if (!usable_) return;
    columns_.resize(static_cast<size_t>(kBatch));
    cache_.resize(static_cast<size_t>(kBatch));
  }

  bool usable() const { return usable_; }
  uint64_t minBuckets() const { return min_buckets_; }

  void reach(uint64_t end) { end_ = (std::max)(end_, end); }

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
      if (!capture_.pyramidSummary(channels_[i], b * kBucket, (b + cache_count_) * kBucket, columns)) {
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

  const GrowingCapture& capture_;
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

class WindowFeed {
 public:
  WindowFeed(const GrowingCapture& capture, const std::vector<uint32_t>& channels, const CaptureFeedOptions& options,
             uint64_t first)
      : capture_(capture),
        channels_(channels),
        options_(options),
        flats_(capture, channels, (first + kBucket - 1) / kBucket, (options.min_skip_samples + kBucket - 1) / kBucket),
        pos_(first) {}

  std::expected<void, FeedError> allocate() {
    const auto bound = static_cast<uint32_t>(channels_.size());
    const uint64_t window_bytes = (uint64_t{capture_.plan().samplesPerWindow()} + 7) / 8;
    try {
      if (options_.before_allocate) options_.before_allocate(AllocationSite::FeedPlanes, window_bytes * bound);
      planes_.assign(bound, std::vector<uint8_t>(static_cast<size_t>(window_bytes)));
      for (const auto& p : planes_) spans_.emplace_back(p);
      flats_.allocate();
    } catch (const std::bad_alloc&) {
      return feedError(FeedFailure::OutOfMemory, "out of memory for samples of " + std::to_string(bound) +
                                                     " bound channels (" + std::to_string(window_bytes * bound) +
                                                     " bytes window)");
    }
    return {};
  }

  uint64_t position() const { return pos_; }

  std::expected<void, FeedError> feedTo(uint64_t end, host::DecodeRingProducer& ring, const StopFlag& stop) {
    flats_.reach(end / kBucket);
    while (pos_ < end) {
      if (stop.load(std::memory_order_relaxed)) return feedError(FeedFailure::Stopped, "stopped");
      if (flats_.usable() && pos_ % kBucket == 0) {
        const uint64_t b = pos_ / kBucket;
        const uint64_t n = flats_.runFrom(b);
        if (n != 0 && n >= flats_.minBuckets()) {
          if (auto skipped = writeSkip(ring, *flats_.levels(b), n * kBucket, stop); !skipped) return skipped;
          pos_ += n * kBucket;
          continue;
        }
      }
      const std::span<const capture::IndexRecord> records = capture_.indexRange(pos_, pos_ + 1);
      if (records.empty()) {
        return feedError(FeedFailure::Failed, "sample " + std::to_string(pos_) + " of capture is not stored");
      }
      const capture::IndexRecord& window = records.front();
      uint64_t piece_end = (std::min)(window.endSample(), end);
      if (flats_.usable()) {
        if (const auto next =
                flats_.nextRunStart((pos_ + kBucket - 1) / kBucket, (piece_end + kBucket - 1) / kBucket)) {
          piece_end = (std::min)(piece_end, *next * kBucket);
        }
      }
      if (loaded_ != window.first_sample_index) {
        const uint64_t bytes = (uint64_t{window.num_samples} + 7) / 8;
        for (uint32_t c = 0; c < channels_.size(); ++c) {
          auto read = capture_.read(channels_[c], window.first_sample_index / 8,
                                    std::span<uint8_t>(planes_[c]).first(static_cast<size_t>(bytes)));
          if (!read) return feedError(FeedFailure::Failed, "capture could not be read: " + read.error());
        }
        loaded_ = window.first_sample_index;
      }
      if (auto written = writePlanes(ring, spans_, pos_ - window.first_sample_index, piece_end - pos_, stop);
          !written) {
        return written;
      }
      pos_ = piece_end;
    }
    return {};
  }

 private:
  const GrowingCapture& capture_;
  const std::vector<uint32_t>& channels_;
  const CaptureFeedOptions& options_;
  FlatBuckets flats_;
  std::vector<std::vector<uint8_t>> planes_;
  std::vector<std::span<const uint8_t>> spans_;
  uint64_t loaded_ = UINT64_MAX;
  uint64_t pos_ = 0;
};

std::expected<void, FeedError> feedGrowing(const GrowingCapture& capture, const CaptureEnd& end,
                                           const std::vector<uint32_t>& channels, const CaptureFeedOptions& options,
                                           host::DecodeRingProducer& ring, const StopFlag& stop) {
  if (options.before_feed) options.before_feed(stop);
  std::optional<capture::CaptureExtent> stored;
  capture::CaptureExtent started;
  for (;;) {
    if (stop.load(std::memory_order_relaxed)) return feedError(FeedFailure::Stopped, "stopped");
    stored = end.stored();
    started = stored ? *stored : capture.publishedExtent();
    if (stored || started.count != 0) break;
    (void)end.waitForStored(kGrowingFeedWait);
  }
  const uint64_t first = started.first_sample;
  WindowFeed feed(capture, channels, options, first);
  if (auto allocated = feed.allocate(); !allocated) return allocated;
  for (;;) {
    if (stop.load(std::memory_order_relaxed)) return feedError(FeedFailure::Stopped, "stopped");
    if (!stored) stored = end.stored();
    if (stored) {
      if (stored->first_sample != first) {
        return feedError(FeedFailure::Failed, "capture's stored samples begin at sample " +
                                                  std::to_string(stored->first_sample) +
                                                  ", but its decode stream began at sample " + std::to_string(first));
      }
      if (first + stored->count < feed.position()) {
        return feedError(FeedFailure::Failed, "decode stream holds " + std::to_string(feed.position() - first) +
                                                  " samples of capture, but capture stored only " +
                                                  std::to_string(stored->count));
      }
      return feed.feedTo(first + stored->count, ring, stop);
    }
    const capture::CaptureExtent published = capture.publishedExtent();
    if (auto fed = feed.feedTo(published.first_sample + published.count, ring, stop); !fed) return fed;
    ring.flush();
    (void)end.waitForStored(kGrowingFeedWait);
  }
}

std::expected<void, std::string> checkChannels(const capture::CapturePlan& plan, const std::vector<uint32_t>& channels) {
  if (channels.empty()) return std::unexpected(std::string("decode job needs at least one bound channel"));
  if (channels.size() > decode::kMaxChannels) {
    return std::unexpected("decode job binds at most " + std::to_string(decode::kMaxChannels) + " channels; " +
                           std::to_string(channels.size()) + " were bound");
  }
  for (const uint32_t c : channels) {
    if (c >= plan.channels()) {
      return std::unexpected("channel " + std::to_string(c) + " is bound, but capture has " +
                             std::to_string(plan.channels()) + " channels");
    }
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
  if (auto checked = checkChannels(buffer.plan(), channels); !checked) return std::unexpected(std::move(checked.error()));
  return FeedFunction([stored = std::make_shared<const StoredCapture>(buffer, extent),
                       end = std::make_shared<const CaptureEnd>(extent), channels = std::move(channels),
                       options = std::move(options)](host::DecodeRingProducer& ring, const StopFlag& stop) {
    return feedGrowing(*stored, *end, channels, options, ring, stop);
  });
}

std::expected<FeedFunction, std::string> growingCaptureFeed(std::shared_ptr<const GrowingCapture> capture,
                                                            std::shared_ptr<const CaptureEnd> end,
                                                            std::vector<uint32_t> channels,
                                                            CaptureFeedOptions options) {
  if (auto checked = checkChannels(capture->plan(), channels); !checked) {
    return std::unexpected(std::move(checked.error()));
  }
  return FeedFunction([capture = std::move(capture), end = std::move(end), channels = std::move(channels),
                       options = std::move(options)](host::DecodeRingProducer& ring, const StopFlag& stop) {
    return feedGrowing(*capture, *end, channels, options, ring, stop);
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
