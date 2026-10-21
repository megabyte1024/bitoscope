/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <span>
#include <string>
#include <vector>

namespace bitoscope::capture {

struct Bucket {
  bool has_zero = false;
  bool has_one = false;
};

class Pyramid {
 public:
  static constexpr uint64_t kBaseBucketSamples = 256;
  static constexpr uint32_t kFanout = 16;

  static std::expected<Pyramid, std::string> create(uint32_t num_channels) {
    if (num_channels == 0) return std::unexpected("pyramid needs at least one channel");
    Pyramid p;
    p.channels_.resize(num_channels);
    return p;
  }

  // WHY: Every append but the last must hold whole base buckets (256 samples): bits are only ever ORed in.
  std::expected<void, std::string> append(uint32_t channel, std::span<const uint8_t> bytes, uint64_t num_samples) {
    if (channel >= channels_.size()) {
      return std::unexpected("channel " + std::to_string(channel) + " does not exist: this pyramid has " +
                             std::to_string(channels_.size()));
    }
    Channel& ch = channels_[channel];
    if (ch.partial) {
      return std::unexpected("previous append to channel " + std::to_string(channel) +
                             " ended part-way through 256-sample bucket; only last append may");
    }
    if (num_samples == 0) return std::unexpected("nothing to append");
    if (bytes.size() != static_cast<size_t>((num_samples + 7) / 8)) {
      return std::unexpected(std::to_string(num_samples) + " samples occupy " + std::to_string((num_samples + 7) / 8) +
                             " bytes, but " + std::to_string(bytes.size()) + " were given");
    }

    if (ch.levels.empty()) ch.levels.emplace_back();
    Level& base = ch.levels[0];
    const uint64_t first_bucket = ch.samples / kBaseBucketSamples;
    const uint64_t full = num_samples / kBaseBucketSamples;
    const uint64_t remainder = num_samples % kBaseBucketSamples;
    const uint64_t new_buckets = full + (remainder != 0 ? 1 : 0);
    grow(base, first_bucket + new_buckets);

    for (uint64_t k = 0; k < full; ++k) {
      uint64_t w[4];
      std::memcpy(w, bytes.data() + k * 32, 32);
      const uint64_t any = w[0] | w[1] | w[2] | w[3];
      const uint64_t all = w[0] & w[1] & w[2] & w[3];
      const uint64_t bucket = first_bucket + k;
      const uint8_t bit = static_cast<uint8_t>(1u << (bucket & 7));
      if (any != 0) base.ones[static_cast<size_t>(bucket >> 3)] |= bit;
      if (all != ~uint64_t{0}) base.zeros[static_cast<size_t>(bucket >> 3)] |= bit;
    }
    if (remainder != 0) {
      const uint8_t* p = bytes.data() + full * 32;
      const uint64_t whole = remainder / 8;
      const uint64_t extra = remainder % 8;
      bool one = false;
      bool zero = false;
      for (uint64_t i = 0; i < whole; ++i) {
        one = one || p[i] != 0;
        zero = zero || p[i] != 0xFF;
      }
      if (extra != 0) {
        const uint8_t mask = static_cast<uint8_t>((1u << extra) - 1u);
        one = one || (p[whole] & mask) != 0;
        zero = zero || (static_cast<uint8_t>(~p[whole]) & mask) != 0;
      }
      const uint64_t bucket = first_bucket + full;
      const uint8_t bit = static_cast<uint8_t>(1u << (bucket & 7));
      if (one) base.ones[static_cast<size_t>(bucket >> 3)] |= bit;
      if (zero) base.zeros[static_cast<size_t>(bucket >> 3)] |= bit;
    }
    ch.partial = remainder != 0;
    ch.samples += num_samples;

    uint64_t lo = first_bucket;
    uint64_t hi = first_bucket + new_buckets;
    for (size_t level = 0; ch.levels[level].buckets > 1; ++level) {
      if (level + 1 == ch.levels.size()) ch.levels.emplace_back();
      Level& child = ch.levels[level];
      Level& parent = ch.levels[level + 1];
      grow(parent, (child.buckets + kFanout - 1) / kFanout);
      const uint64_t parent_lo = lo / kFanout;
      const uint64_t parent_hi = (hi + kFanout - 1) / kFanout;
      for (uint64_t q = parent_lo; q < parent_hi; ++q) {
        const size_t b0 = static_cast<size_t>(2 * q);
        const uint8_t one = static_cast<uint8_t>((b0 < child.ones.size() ? child.ones[b0] : 0) |
                                                 (b0 + 1 < child.ones.size() ? child.ones[b0 + 1] : 0));
        const uint8_t zero = static_cast<uint8_t>((b0 < child.zeros.size() ? child.zeros[b0] : 0) |
                                                  (b0 + 1 < child.zeros.size() ? child.zeros[b0 + 1] : 0));
        const uint8_t bit = static_cast<uint8_t>(1u << (q & 7));
        if (one != 0) parent.ones[static_cast<size_t>(q >> 3)] |= bit;
        if (zero != 0) parent.zeros[static_cast<size_t>(q >> 3)] |= bit;
      }
      lo = parent_lo;
      hi = parent_hi;
    }
    return {};
  }

  uint64_t sampleCount(uint32_t channel) const { return channel < channels_.size() ? channels_[channel].samples : 0; }

  std::expected<void, std::string> summary(uint32_t channel, uint64_t begin, uint64_t end, std::span<Bucket> out) const {
    if (channel >= channels_.size()) {
      return std::unexpected("channel " + std::to_string(channel) + " does not exist: this pyramid has " +
                             std::to_string(channels_.size()));
    }
    const Channel& ch = channels_[channel];
    if (ch.samples == 0) return std::unexpected("nothing has been captured on channel " + std::to_string(channel) + " yet");
    if (out.empty()) return std::unexpected("summary needs at least one column");
    if (begin >= end) return std::unexpected("range [" + std::to_string(begin) + ", " + std::to_string(end) + ") is empty");
    if (end > ch.samples) {
      return std::unexpected("range ends at " + std::to_string(end) + " but only " + std::to_string(ch.samples) +
                             " samples have been captured");
    }
    const uint64_t range = end - begin;
    const uint64_t columns = out.size();
    const uint64_t width = range / columns;
    if (width < kBaseBucketSamples) {
      return std::unexpected("columns of " + std::to_string(width) +
                             " samples are narrower than pyramid's 256-sample base bucket: read segments directly");
    }

    size_t level = 0;
    uint64_t bucket_width = kBaseBucketSamples;
    while (level + 1 < ch.levels.size() && bucket_width * kFanout <= width / 4) {
      bucket_width *= kFanout;
      ++level;
    }
    const Level& l = ch.levels[level];
    const uint64_t q = range / columns;
    const uint64_t r = range % columns;
    for (uint64_t j = 0; j < columns; ++j) {
      const uint64_t s = begin + q * j + r * j / columns;
      const uint64_t e = begin + q * (j + 1) + r * (j + 1) / columns;
      const uint64_t first = s / bucket_width;
      const uint64_t last = std::min((e - 1) / bucket_width, l.buckets - 1);
      out[static_cast<size_t>(j)].has_zero = anyBit(l.zeros, first, last);
      out[static_cast<size_t>(j)].has_one = anyBit(l.ones, first, last);
    }
    return {};
  }

  size_t memoryBytes() const {
    size_t total = 0;
    for (const Channel& ch : channels_) {
      for (const Level& l : ch.levels) total += l.ones.size() + l.zeros.size();
    }
    return total;
  }

  size_t levelCount(uint32_t channel) const { return channel < channels_.size() ? channels_[channel].levels.size() : 0; }

  uint8_t planeByteForTest(uint32_t channel, size_t level, size_t byte, bool ones) const {
    if (channel >= channels_.size() || level >= channels_[channel].levels.size()) return 0;
    const Level& l = channels_[channel].levels[level];
    const std::vector<uint8_t>& plane = ones ? l.ones : l.zeros;
    return byte < plane.size() ? plane[byte] : 0;
  }

 private:
  struct Level {
    std::vector<uint8_t> ones;
    std::vector<uint8_t> zeros;
    uint64_t buckets = 0;
  };
  struct Channel {
    uint64_t samples = 0;
    bool partial = false;
    std::vector<Level> levels;
  };

  static void grow(Level& level, uint64_t buckets) {
    level.buckets = buckets;
    const size_t bytes = static_cast<size_t>((buckets + 7) / 8);
    if (level.ones.size() < bytes) {
      level.ones.resize(bytes, 0);
      level.zeros.resize(bytes, 0);
    }
  }

  static bool anyBit(const std::vector<uint8_t>& plane, uint64_t first, uint64_t last) {
    if (first > last) return false;
    const size_t fb = static_cast<size_t>(first >> 3);
    const size_t lb = static_cast<size_t>(last >> 3);
    const uint8_t first_mask = static_cast<uint8_t>(0xFFu << (first & 7));
    const uint8_t last_mask = static_cast<uint8_t>(0xFFu >> (7 - (last & 7)));
    if (fb == lb) return (plane[fb] & first_mask & last_mask) != 0;
    if ((plane[fb] & first_mask) != 0 || (plane[lb] & last_mask) != 0) return true;
    size_t i = fb + 1;
    while (i + 8 <= lb) {
      uint64_t word;
      std::memcpy(&word, plane.data() + i, 8);
      if (word != 0) return true;
      i += 8;
    }
    for (; i < lb; ++i) {
      if (plane[i] != 0) return true;
    }
    return false;
  }

  std::vector<Channel> channels_;
};

}
