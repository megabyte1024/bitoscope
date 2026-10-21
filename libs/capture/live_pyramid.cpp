/*
 * SPDX-License-Identifier: MIT
 */

#include "live_pyramid.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <optional>

namespace bitoscope::capture {

namespace {

constexpr uint64_t kBucket = Pyramid::kBaseBucketSamples;
constexpr uint64_t kFanout = Pyramid::kFanout;

uint8_t rangeBits(const uint8_t* raw, uint64_t s, uint64_t e) {
  bool one = false;
  bool zero = false;
  const auto bit = [&](uint64_t i) { ((raw[i >> 3] >> (i & 7)) & 1) != 0 ? one = true : zero = true; };
  for (; s < e && (s & 7) != 0; ++s) bit(s);
  for (; s + 64 <= e && !(one && zero); s += 64) {
    uint64_t w;
    std::memcpy(&w, raw + (s >> 3), 8);
    one = one || w != 0;
    zero = zero || w != ~uint64_t{0};
  }
  for (; s + 8 <= e && !(one && zero); s += 8) {
    one = one || raw[s >> 3] != 0;
    zero = zero || raw[s >> 3] != 0xFF;
  }
  for (; s < e && !(one && zero); ++s) bit(s);
  return static_cast<uint8_t>((one ? kBucketHasOne : 0) | (zero ? kBucketHasZero : 0));
}

}

uint32_t maxWindowBuckets(uint32_t samples_per_window) {
  return static_cast<uint32_t>((uint64_t{samples_per_window} + kBucket - 1) / kBucket + 1);
}

uint64_t windowBucketBytes(uint32_t channels, uint32_t samples_per_window) {
  return uint64_t{channels} * maxWindowBuckets(samples_per_window);
}

uint64_t pyramidHistoryScratchBytes(uint32_t channels, uint32_t samples_per_window) {
  return (uint64_t{samples_per_window} + 7) / 8 + windowBucketBytes(channels, samples_per_window);
}

uint32_t windowBucketBits(uint64_t first_sample, uint32_t num_samples, std::span<const uint8_t> raw,
                          std::span<uint8_t> out) {
  if (num_samples == 0) return 0;
  const uint64_t first = first_sample / kBucket;
  const uint64_t last = (first_sample + num_samples - 1) / kBucket;
  const uint32_t count = static_cast<uint32_t>(last - first + 1);
  const uint64_t end = first_sample + num_samples;
  for (uint32_t k = 0; k < count; ++k) {
    const uint64_t s = (std::max)(first_sample, (first + k) * kBucket) - first_sample;
    const uint64_t e = (std::min)(end, (first + k + 1) * kBucket) - first_sample;
    if (e - s == kBucket && (s & 7) == 0) {
      uint64_t w[4];
      std::memcpy(w, raw.data() + (s >> 3), 32);
      const bool one = (w[0] | w[1] | w[2] | w[3]) != 0;
      const bool zero = (w[0] & w[1] & w[2] & w[3]) != ~uint64_t{0};
      out[k] = static_cast<uint8_t>((one ? kBucketHasOne : 0) | (zero ? kBucketHasZero : 0));
      continue;
    }
    out[k] = rangeBits(raw.data(), s, e);
  }
  return count;
}

uint64_t pyramidSamplesBound(uint32_t channels, uint64_t arena_bytes) {
  // WHY: 8 samples x 255: no LZ4 sequence decodes to more than 255 times its bytes; a new codec must keep that.
  constexpr uint64_t kSamplesPerStoredByte = 8 * 255;
  constexpr uint64_t kMax = std::numeric_limits<uint64_t>::max();
  if (channels == 0) return 0;
  const uint64_t whole = arena_bytes / channels;
  const uint64_t rest = arena_bytes % channels * kSamplesPerStoredByte / channels;
  if (whole > (kMax - rest) / kSamplesPerStoredByte) return kMax;
  return whole * kSamplesPerStoredByte + rest;
}

uint64_t pyramidCapacity(uint32_t channels, uint64_t total_samples, uint64_t arena_bytes) {
  return (std::min)(total_samples, pyramidSamplesBound(channels, arena_bytes));
}

uint64_t pyramidPlaneBytes(uint32_t channels, uint64_t total_samples, uint64_t arena_bytes) {
  return LivePyramid::bytesFor(channels, pyramidCapacity(channels, total_samples, arena_bytes));
}

uint64_t LivePyramid::bytesFor(uint32_t channels, uint64_t capacity_samples) {
  uint64_t per_plane = 0;
  uint64_t buckets = std::max<uint64_t>(1, (capacity_samples + kBucket - 1) / kBucket);
  for (;;) {
    per_plane += (buckets + 7) / 8;
    if (buckets == 1) break;
    buckets = (buckets + kFanout - 1) / kFanout;
  }
  return per_plane * channels * 2;
}

std::expected<std::unique_ptr<LivePyramid>, std::string> LivePyramid::create(uint32_t channels,
                                                                             uint64_t capacity_samples) {
  if (channels == 0) return std::unexpected("pyramid needs at least one channel");
  std::unique_ptr<LivePyramid> p(new LivePyramid);
  p->channels_ = channels;
  uint64_t buckets = std::max<uint64_t>(1, (capacity_samples + kBucket - 1) / kBucket);
  for (;;) {
    p->levels_.push_back(Level{buckets, p->bytes_per_channel_});
    p->bytes_per_channel_ += (buckets + 7) / 8;
    if (buckets == 1) break;
    buckets = (buckets + kFanout - 1) / kFanout;
  }
  const uint64_t total = p->bytes_per_channel_ * channels;
  // WHY: calloc, not value-initialized new or a vector: pages must stay untouched until a fold writes them.
  p->ones_.reset(static_cast<uint8_t*>(std::calloc(static_cast<size_t>(total), 1)));
  p->zeros_.reset(static_cast<uint8_t*>(std::calloc(static_cast<size_t>(total), 1)));
  if (!p->ones_ || !p->zeros_) {
    return std::unexpected("out of memory for capture's pyramid (" + std::to_string(total * 2) + " bytes)");
  }
  return p;
}

void LivePyramid::FreePlane::operator()(uint8_t* plane) const noexcept { std::free(plane); }

template <bool Shared>
void LivePyramid::setBit(uint8_t* plane, size_t at, uint8_t bit) {
  std::atomic_ref<uint8_t> cell(plane[at]);
  const uint8_t was = cell.load(std::memory_order_relaxed);
  if ((was & bit) != 0) return;
  if constexpr (Shared) {
    cell.fetch_or(bit, std::memory_order_relaxed);
  } else {
    cell.store(static_cast<uint8_t>(was | bit), std::memory_order_relaxed);
  }
}

template <bool Shared>
void LivePyramid::foldWith(const WindowBuckets& window) {
  const Level& base = levels_[0];
  for (uint32_t c = 0; c < channels_; ++c) {
    const uint8_t* bits = window.bits.data() + uint64_t{c} * window.stride;
    for (uint32_t k = 0; k < window.count; ++k) {
      const uint64_t b = window.first_bucket + k;
      if (b >= base.buckets) break;
      const uint8_t mask = static_cast<uint8_t>(1u << (b & 7));
      if ((bits[k] & kBucketHasOne) != 0) setBit<Shared>(ones_.get(), index(c, base, b >> 3), mask);
      if ((bits[k] & kBucketHasZero) != 0) setBit<Shared>(zeros_.get(), index(c, base, b >> 3), mask);
    }
    uint64_t lo = window.first_bucket;
    uint64_t hi = (std::min)(window.first_bucket + window.count, base.buckets);
    for (size_t level = 0; level + 1 < levels_.size() && lo < hi; ++level) {
      const Level& child = levels_[level];
      const Level& parent = levels_[level + 1];
      const uint64_t plo = lo / kFanout;
      const uint64_t phi = (hi + kFanout - 1) / kFanout;
      for (uint64_t q = plo; q < phi; ++q) {
        const uint64_t b0 = 2 * q;
        const uint64_t child_bytes = (child.buckets + 7) / 8;
        const auto at = [&](const uint8_t* plane, uint64_t byte) -> uint8_t {
          return byte < child_bytes ? loadByte(plane, index(c, child, byte)) : uint8_t{0};
        };
        const uint8_t mask = static_cast<uint8_t>(1u << (q & 7));
        if ((at(ones_.get(), b0) | at(ones_.get(), b0 + 1)) != 0) setBit<Shared>(ones_.get(), index(c, parent, q >> 3), mask);
        if ((at(zeros_.get(), b0) | at(zeros_.get(), b0 + 1)) != 0) setBit<Shared>(zeros_.get(), index(c, parent, q >> 3), mask);
      }
      lo = plo;
      hi = phi;
    }
  }
}

void LivePyramid::fold(const WindowBuckets& window, bool shared) {
  if (window.count == 0) return;
  if (shared) {
    foldWith<true>(window);
  } else {
    foldWith<false>(window);
  }
}

bool LivePyramid::anyBit(const uint8_t* plane, uint32_t channel, const Level& level, uint64_t first,
                         uint64_t last) const {
  if (first > last) return false;
  const uint64_t fb = first >> 3;
  const uint64_t lb = last >> 3;
  const uint8_t first_mask = static_cast<uint8_t>(0xFFu << (first & 7));
  const uint8_t last_mask = static_cast<uint8_t>(0xFFu >> (7 - (last & 7)));
  const auto at = [&](uint64_t byte) { return loadByte(plane, index(channel, level, byte)); };
  if (fb == lb) return (at(fb) & first_mask & last_mask) != 0;
  if ((at(fb) & first_mask) != 0 || (at(lb) & last_mask) != 0) return true;
  for (uint64_t i = fb + 1; i < lb; ++i) {
    if (at(i) != 0) return true;
  }
  return false;
}

std::optional<Bucket> LivePyramid::exactRange(uint32_t channel, uint64_t begin, uint64_t end,
                                              uint64_t available) const {
  if (channel >= channels_ || begin >= end || end > available) return std::nullopt;
  if (begin % kBucket != 0 || end % kBucket != 0) return std::nullopt;
  Bucket out;
  uint64_t s = begin;
  while (s < end) {
    size_t level = 0;
    uint64_t width = kBucket;
    while (level + 1 < levels_.size() && s % (width * kFanout) == 0 && s + width * kFanout <= end) {
      width *= kFanout;
      ++level;
    }
    const Level& l = levels_[level];
    const uint64_t bucket = s / width;
    if (bucket < l.buckets) {
      if (!out.has_one) out.has_one = anyBit(ones_.get(), channel, l, bucket, bucket);
      if (!out.has_zero) out.has_zero = anyBit(zeros_.get(), channel, l, bucket, bucket);
    }
    s += width;
    if (out.has_one && out.has_zero) break;
  }
  return out;
}

std::expected<void, std::string> LivePyramid::summary(uint32_t channel, uint64_t begin, uint64_t end,
                                                      uint64_t available, std::span<Bucket> out) const {
  if (channel >= channels_) {
    return std::unexpected("channel " + std::to_string(channel) + " does not exist: this pyramid has " +
                           std::to_string(channels_));
  }
  if (available == 0) return std::unexpected("nothing has been captured yet");
  if (out.empty()) return std::unexpected("summary needs at least one column");
  if (begin >= end) return std::unexpected("range [" + std::to_string(begin) + ", " + std::to_string(end) + ") is empty");
  if (end > available) {
    return std::unexpected("range ends at " + std::to_string(end) + " but only " + std::to_string(available) +
                           " samples have been captured");
  }
  const uint64_t range = end - begin;
  const uint64_t columns = out.size();
  const uint64_t width = range / columns;
  if (width < kBucket) {
    return std::unexpected("columns of " + std::to_string(width) +
                           " samples are narrower than pyramid's 256-sample base bucket: read segments directly");
  }
  size_t level = 0;
  uint64_t bucket_width = kBucket;
  const uint64_t available_buckets = (available + kBucket - 1) / kBucket;
  uint64_t level_buckets = available_buckets;
  while (level + 1 < levels_.size() && level_buckets > 1 && bucket_width * kFanout <= width / 4) {
    bucket_width *= kFanout;
    level_buckets = (level_buckets + kFanout - 1) / kFanout;
    ++level;
  }
  const Level& l = levels_[level];
  const uint64_t q = range / columns;
  const uint64_t r = range % columns;
  for (uint64_t j = 0; j < columns; ++j) {
    const uint64_t s = begin + q * j + r * j / columns;
    const uint64_t e = begin + q * (j + 1) + r * (j + 1) / columns;
    const uint64_t first = s / bucket_width;
    const uint64_t last = std::min((e - 1) / bucket_width, l.buckets - 1);
    out[static_cast<size_t>(j)].has_zero = anyBit(zeros_.get(), channel, l, first, last);
    out[static_cast<size_t>(j)].has_one = anyBit(ones_.get(), channel, l, first, last);
  }
  return {};
}

}
