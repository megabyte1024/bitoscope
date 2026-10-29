/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

// WHY: Not thread-safe (mutable block cache): one view per thread.
// level()/nextEdge() cannot return errors: check readError() when done reading.

#include <bit>
#include <cstdint>
#include <cstring>
#include <expected>
#include <format>
#include <functional>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "capture_extent.h"
#include "decoder.h"
#include "raw_capture_buffer.h"

namespace bitoscope::capture {

class RawBufferSignalView final : public decode::SignalView {
 public:
  using BeforeAllocate = std::function<void(uint64_t bytes)>;

  static std::expected<RawBufferSignalView, std::string> create(
      const RawCaptureBuffer& buffer, CaptureExtent extent, ExtentTail tail = ExtentTail::Exact,
      BeforeAllocate before_allocate = {}) {
    if (!buffer.finalized()) return std::unexpected("capture buffer is not finalized");
    if (buffer.windowCount() == 0) return std::unexpected("capture holds no samples");

    if (extent.count == 0) {
      return std::unexpected(
          "capture's extent was not set: CaptureExtent defaults to {0, 0}, and view "
          "must be told which samples it holds");
    }
    if (extent.first_sample > (std::numeric_limits<uint64_t>::max)() - extent.count) {
      return std::unexpected(std::format(
          "capture's extent overflows: first_sample {} + count {} does not fit in 64 bits",
          extent.first_sample, extent.count));
    }
    const uint64_t extent_end = extent.first_sample + extent.count;
    const auto records = buffer.indexRange(0, buffer.sampleCount());
    const bool runs_past_inside_last_window = tail == ExtentTail::InsideLastWindow &&
                                              buffer.sampleCount() > extent_end &&
                                              !records.empty() &&
                                              records.back().first_sample_index < extent_end;
    const bool runs_past_trimmed = tail == ExtentTail::Trimmed && buffer.sampleCount() > extent_end;
    if (buffer.sampleCount() != extent_end && !runs_past_inside_last_window && !runs_past_trimmed) {
      return std::unexpected(std::format(
          "buffer holds {} samples, but capture's extent is [{}, {})",
          buffer.sampleCount(), extent.first_sample, extent_end));
    }

    for (size_t i = 0; i + 1 < records.size(); ++i) {
      const IndexRecord& r = records[i];
      if (r.num_samples % 8 != 0) {
        return std::unexpected(std::format(
            "window at sample {} holds {} samples, not whole number of bytes, and another follows",
            r.first_sample_index, r.num_samples));
      }
    }

    RawBufferSignalView view;
    view.buffer_ = &buffer;
    view.extent_ = extent;
    try {
      if (before_allocate) before_allocate(uint64_t{buffer.plan().channels()} * sizeof(Cached));
      view.cache_.assign(buffer.plan().channels(), Cached{});
      const uint64_t max_window_bytes = (uint64_t{buffer.plan().samplesPerWindow()} + 7) / 8;
      const uint64_t block_bytes = max_window_bytes + kWordOverlapBytes;
      for (Cached& c : view.cache_) {
        if (before_allocate) before_allocate(block_bytes);
        c.bytes.resize(static_cast<size_t>(block_bytes));
      }
    } catch (const std::bad_alloc&) {
      return std::unexpected(
          "out of memory for signal view's per-channel decode blocks");
    }
    return view;
  }

  uint32_t channelCount() const override { return buffer_->plan().channels(); }
  uint64_t sampleCount() const override { return extent_.count; }
  uint64_t sampleRateMilliHz() const override { return buffer_->plan().sampleRateMilliHz(); }

  bool level(uint32_t channel, uint64_t index) const override {
    if (failed() || channel >= channelCount() || index >= sampleCount()) return false;
    const uint64_t a = extent_.first_sample + index;
    const std::optional<uint8_t> byte = byteAt(channel, a / 8);
    return byte.has_value() && ((*byte >> (a % 8)) & 1u) != 0;
  }

  std::string readError() const override { return read_error_; }

  // WHY: Address BUFFER-relative: view sample i is not buffer byte i / 8 unless first_sample % 8 == 0.
  std::optional<uint64_t> nextEdge(uint32_t channel, uint64_t from,
                                   decode::EdgeKind edge) const override {
    const uint64_t view_count = sampleCount();
    if (failed() || channel >= channelCount() || from >= view_count || from + 1 >= view_count) {
      return std::nullopt;
    }
    const uint64_t count = extent_.first_sample + view_count;
    const uint64_t start = extent_.first_sample + from;

    uint64_t carry = level(channel, from) ? 1u : 0u;
    if (failed()) return std::nullopt;
    uint64_t i = start + 1;
    uint64_t b = i >> 3;

    {
      const uint64_t rem = count - b * 8;
      const uint32_t n = static_cast<uint32_t>(rem >= 64 ? 8 : (rem + 7) / 8);
      const std::optional<uint64_t> loaded = loadBytes(channel, b, n);
      if (!loaded) return std::nullopt;
      const uint64_t w = *loaded;
      uint64_t x = edgeMaskOf(w, carry, edge) & ~timeMaskUpTo(i & 7);
      if (rem < 64) x &= timeMaskUpTo(rem);
      if (x != 0) {
        return b * 8 + static_cast<uint64_t>(std::countr_zero(x)) - extent_.first_sample;
      }
      if (rem <= 64) return std::nullopt;
      carry = (w >> 63) & 1u;
      b += 8;
    }

    for (; b * 8 + 64 <= count; b += 8) {
      b = skipFillWords(channel, b, count / 8, carry);
      if (failed()) return std::nullopt;
      if (b * 8 + 64 > count) break;
      const std::optional<uint64_t> loaded = loadBytes(channel, b, 8);
      if (!loaded) return std::nullopt;
      const uint64_t w = *loaded;
      const uint64_t x = edgeMaskOf(w, carry, edge);
      if (x != 0) {
        return b * 8 + static_cast<uint64_t>(std::countr_zero(x)) - extent_.first_sample;
      }
      carry = (w >> 63) & 1u;
    }

    const uint64_t rem = count - b * 8;
    if (rem == 0) return std::nullopt;
    const std::optional<uint64_t> loaded = loadBytes(channel, b, static_cast<uint32_t>((rem + 7) / 8));
    if (!loaded) return std::nullopt;
    const uint64_t w = *loaded;
    const uint64_t x = edgeMaskOf(w, carry, edge) & timeMaskUpTo(rem);
    if (x != 0) {
      return b * 8 + static_cast<uint64_t>(std::countr_zero(x)) - extent_.first_sample;
    }
    return std::nullopt;
  }

 private:
  static constexpr uint64_t kWordOverlapBytes = 7;

  struct Cached {
    bool valid = false;
    uint64_t first_byte = 0;
    uint64_t byte_count = 0;
    std::vector<uint8_t> bytes;
  };

  RawBufferSignalView() = default;

  bool relocate(uint32_t channel, uint64_t byte) const {
    Cached& c = cache_[channel];
    const auto records = buffer_->indexRange(byte * 8, byte * 8 + 1);
    const IndexRecord& rec = records.front();
    const uint64_t window_first_byte = rec.first_sample_index / 8;
    const uint64_t window_bytes = (uint64_t{rec.num_samples} + 7) / 8;
    if (auto r = buffer_->read(channel, window_first_byte,
                               std::span<uint8_t>(c.bytes.data(), static_cast<size_t>(window_bytes)));
        !r) {
      c.valid = false;
      c.byte_count = 0;
      fail(r.error());
      return false;
    }
    uint64_t byte_count = window_bytes;
    const uint64_t total_buffer_bytes = (buffer_->sampleCount() + 7) / 8;
    const uint64_t after = window_first_byte + window_bytes;
    if (after < total_buffer_bytes) {
      const uint64_t overlap = (std::min)(kWordOverlapBytes, total_buffer_bytes - after);
      if (buffer_->read(channel, after,
                        std::span<uint8_t>(c.bytes.data() + window_bytes, static_cast<size_t>(overlap)))) {
        byte_count += overlap;
      }
    }
    c.first_byte = window_first_byte;
    c.byte_count = byte_count;
    c.valid = true;
    return true;
  }

  bool ensureCached(uint32_t channel, uint64_t byte) const {
    const Cached& c = cache_[channel];
    if (c.valid && byte >= c.first_byte && byte < c.first_byte + c.byte_count) return true;
    return relocate(channel, byte);
  }

  std::optional<uint8_t> byteAt(uint32_t channel, uint64_t byte) const {
    if (!ensureCached(channel, byte)) return std::nullopt;
    const Cached& c = cache_[channel];
    return c.bytes[static_cast<size_t>(byte - c.first_byte)];
  }

  std::optional<uint64_t> loadBytes(uint32_t channel, uint64_t byte, uint32_t n) const {
    if (!ensureCached(channel, byte)) return std::nullopt;
    if (byte + n > cache_[channel].first_byte + cache_[channel].byte_count) {
      if (!relocate(channel, byte)) return std::nullopt;
    }
    const Cached& c = cache_[channel];
    uint64_t w = 0;
    if (byte + n <= c.first_byte + c.byte_count) {
      std::memcpy(&w, c.bytes.data() + (byte - c.first_byte), n);
      return w;
    }
    uint8_t tmp[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    if (auto r = buffer_->read(channel, byte, std::span<uint8_t>(tmp, n)); !r) {
      fail(r.error());
      return std::nullopt;
    }
    std::memcpy(&w, tmp, n);
    return w;
  }

  static uint64_t timeMaskUpTo(uint64_t n) {
    return n >= 64 ? (std::numeric_limits<uint64_t>::max)() : ((uint64_t{1} << n) - 1);
  }

  static uint64_t edgeMaskOf(uint64_t w, uint64_t carry, decode::EdgeKind edge) {
    const uint64_t changed = w ^ ((w << 1) | carry);
    if (edge == decode::EdgeKind::Any) return changed;
    return edge == decode::EdgeKind::Rising ? (changed & w) : (changed & ~w);
  }

  uint64_t skipFillWords(uint32_t channel, uint64_t b, uint64_t whole_bytes, uint64_t carry) const {
    const uint64_t fill = carry != 0 ? (std::numeric_limits<uint64_t>::max)() : uint64_t{0};
    while (b + 8 <= whole_bytes && loadBytes(channel, b, 8) == std::optional<uint64_t>(fill)) b += 8;
    return b;
  }

  bool failed() const { return !read_error_.empty(); }

  void fail(const std::string& why) const {
    if (read_error_.empty()) read_error_ = "capture could not be read: " + why;
  }

  const RawCaptureBuffer* buffer_ = nullptr;
  CaptureExtent extent_;
  mutable std::string read_error_;
  mutable std::vector<Cached> cache_;
};

}
