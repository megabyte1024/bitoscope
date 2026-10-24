/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <cstring>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>

#include "chunk_view.h"
#include "copy_executor.h"
#include "sample_encoding.h"
#include "sample_window.h"

// WHY: Preprocessor guard, not if constexpr: <emmintrin.h> is absent off x86 and a discarded branch must still parse.
#ifndef BITOSCOPE_WINDOW_ASSEMBLER_SSE2
#if defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#define BITOSCOPE_WINDOW_ASSEMBLER_SSE2 1
#else
#define BITOSCOPE_WINDOW_ASSEMBLER_SSE2 0
#endif
#endif

#if BITOSCOPE_WINDOW_ASSEMBLER_SSE2
#include <emmintrin.h>
#endif

namespace bitoscope::host {

struct AssemblerConfig {
  uint32_t samples_per_window = 0;
  uint32_t num_buffers = 0;
  CopyExecutor* executor = nullptr;
  uint32_t parallel_slice_samples = 0;
};

class WindowAssembler {
 public:
  using WindowFn = std::function<void(WindowLease)>;

  static std::expected<WindowAssembler, std::string> create(AssemblerConfig config,
                                                             uint32_t num_channels) {
    if (config.samples_per_window == 0 || config.samples_per_window % 8 != 0) {
      return std::unexpected("samples_per_window must be positive multiple of 8, got " +
                              std::to_string(config.samples_per_window));
    }
    if (config.num_buffers < 2) {
      return std::unexpected(
          "num_buffers must be at least 2 so one window can be filled while "
          "consumer still holds previous one, got " +
          std::to_string(config.num_buffers));
    }
    if (num_channels == 0) {
      return std::unexpected("num_channels must be non-zero");
    }
    if (config.parallel_slice_samples % SlicePlan::kAlign != 0) {
      return std::unexpected(
          "parallel_slice_samples must be 0 or multiple of " + std::to_string(SlicePlan::kAlign) +
          " so two slices never share window byte, got " +
          std::to_string(config.parallel_slice_samples));
    }
    return WindowAssembler(config, num_channels);
  }

  WindowAssembler(WindowAssembler&&) = default;
  WindowAssembler(const WindowAssembler&) = delete;
  WindowAssembler& operator=(const WindowAssembler&) = delete;

  std::expected<void, std::string> submit(const ChunkView& chunk, const WindowFn& on_window) {
    if (wait_ended_) {
      return std::unexpected(
          "this assembler gave up waiting for window buffer part way through chunk, so what "
          "it holds is not whole stream and it takes no more chunks");
    }
    if (chunk.num_channels != num_channels_) {
      return std::unexpected("chunk has " + std::to_string(chunk.num_channels) +
                              " channels, this assembler is configured for " +
                              std::to_string(num_channels_));
    }
    if (chunk.encoding != SampleEncoding::ChannelMajorPacked &&
        chunk.encoding != SampleEncoding::SampleMajorPacked) {
      return std::unexpected("unsupported SampleEncoding " +
                              std::to_string(static_cast<uint32_t>(chunk.encoding)));
    }
    const std::span<const uint8_t> bytes = chunk.payload;
    const size_t expected_bytes =
        expectedPayloadBytes(chunk.encoding, num_channels_, chunk.num_samples);
    if (bytes.size() != expected_bytes) {
      return std::unexpected("chunk payload is " + std::to_string(bytes.size()) +
                              " bytes, expected " + std::to_string(expected_bytes));
    }

    if (started_) {
      if (chunk.sample_rate_milli_hz != sample_rate_milli_hz_) {
        return std::unexpected("chunk sample rate " + std::to_string(chunk.sample_rate_milli_hz) +
                                " mHz differs from this capture's " +
                                std::to_string(sample_rate_milli_hz_) +
                                " mHz - capture has one rate, so this is malformed stream, or "
                                "new capture that needs its own assembler");
      }
      if (chunk.first_sample_index < expected_next_index_) {
        return std::unexpected("chunk starts at sample " + std::to_string(chunk.first_sample_index) +
                                " but next expected is " + std::to_string(expected_next_index_) +
                                " - overlapping, duplicated or out of order (or capture that "
                                "restarted its numbering, which needs new assembler)");
      }
    }

    if (!started_) {
      started_ = true;
      sample_rate_milli_hz_ = chunk.sample_rate_milli_hz;
      window_first_sample_ = chunk.first_sample_index;
    } else if (chunk.first_sample_index > expected_next_index_) {
      cutAtGap(chunk.first_sample_index - expected_next_index_, chunk.first_sample_index,
               on_window);
    }
    expected_next_index_ = chunk.first_sample_index + chunk.num_samples;

    uint32_t consumed = 0;
    while (consumed < chunk.num_samples) {
      if (!holdingWindow()) {
        auto lease = pool_->acquireWaiting(wait_limit_);
        if (!lease) {
          wait_ended_ = lease.error();
          if (lease.error() == WaitEnd::TimedOut) {
            return std::unexpected("no window buffer was freed within " +
                                    std::to_string(wait_limit_.timeout_ms) +
                                    " ms: every one is still held by whatever windows were "
                                    "handed to");
          }
          return std::unexpected("wait for free window buffer was abandoned");
        }
        current_ = std::move(*lease);
      }
      const uint32_t remaining_in_window = config_.samples_per_window - filled_;
      const uint32_t take = std::min(remaining_in_window, chunk.num_samples - consumed);

      copySamples(bytes, chunk.encoding, consumed, take);
      filled_ += take;
      consumed += take;

      if (filled_ == config_.samples_per_window) {
        emit(on_window);
      }
    }
    return {};
  }

  void flush(const WindowFn& on_window) {
    if (filled_ > 0) {
      emit(on_window);
    }
  }

  uint64_t gapSamples() const { return gap_total_; }

  void setWaitLimit(WaitLimit limit) { wait_limit_ = std::move(limit); }

  void wakeWaiters() { pool_->wakeWaiters(); }

  std::optional<WaitEnd> waitEnded() const { return wait_ended_; }

  const WindowLease* fillingWindow() const { return holdingWindow() ? &*current_ : nullptr; }
  uint64_t fillingFirstSample() const { return window_first_sample_; }
  uint32_t filledSamples() const { return filled_; }

  uint32_t poolCapacityForTest() const { return pool_->capacity(); }
  uint64_t poolStorageBytesForTest() const { return pool_->storageBytes(); }

  void releaseWindows() {
    beginWindow();
    pool_->releaseStorage();
  }

 private:
  // WHY: Pool held by pointer: it owns atomics and every WindowLease points at it, so it must not move.
  WindowAssembler(AssemblerConfig config, uint32_t num_channels)
      : config_(config),
        num_channels_(num_channels),
        pool_(std::make_unique<WindowPool>(config.num_buffers, num_channels,
                                            config.samples_per_window)) {
    beginWindow();
  }

  bool holdingWindow() const { return current_.has_value() && current_->valid(); }

  void cutAtGap(uint64_t gap, uint64_t next_index, const WindowFn& on_window) {
    if (filled_ > 0) {
      emit(on_window);
    }
    window_first_sample_ = next_index;
    gap_pending_ += gap;
    gap_total_ += gap;
  }

  void beginWindow() {
    current_.reset();
    filled_ = 0;
  }

  // WHY: acquire() does not clear a slot: a partial window would leak an earlier window's bits past num_samples.
  void zeroTail() {
    if (filled_ >= config_.samples_per_window) return;
    for (uint32_t ch = 0; ch < num_channels_; ++ch) {
      const std::span<uint8_t> dst = current_->channel(ch);
      const size_t first_byte = filled_ / 8;
      if (filled_ % 8 != 0) {
        const unsigned used_bits = filled_ % 8;
        const uint8_t keep_mask = static_cast<uint8_t>((1u << used_bits) - 1u);
        dst[first_byte] &= keep_mask;
        for (size_t byte = first_byte + 1; byte < dst.size(); ++byte) {
          dst[byte] = 0;
        }
      } else {
        for (size_t byte = first_byte; byte < dst.size(); ++byte) {
          dst[byte] = 0;
        }
      }
    }
  }

  void emit(const WindowFn& on_window) {
    zeroTail();
    WindowInfo& info = current_->info();
    info.first_sample_index = window_first_sample_;
    info.sample_rate_milli_hz = sample_rate_milli_hz_;
    info.num_channels = num_channels_;
    info.num_samples = filled_;
    info.gap_samples_before = gap_pending_;
    gap_pending_ = 0;
    on_window(std::move(*current_));
    window_first_sample_ += filled_;
    beginWindow();
  }

  static constexpr uint64_t transpose8x8(uint64_t x) {
    uint64_t t = (x ^ (x >> 7)) & 0x00AA00AA00AA00AAull;
    x = x ^ t ^ (t << 7);
    t = (x ^ (x >> 14)) & 0x0000CCCC0000CCCCull;
    x = x ^ t ^ (t << 14);
    t = (x ^ (x >> 28)) & 0x00000000F0F0F0F0ull;
    x = x ^ t ^ (t << 28);
    return x;
  }

  static uint64_t gatherRows(const uint8_t* src, size_t stride) {
    uint64_t x = 0;
    for (unsigned k = 0; k < 8; ++k) {
      x |= static_cast<uint64_t>(src[k * stride]) << (8 * k);
    }
    return x;
  }

  // WHY: Copy kernels read no assembler state: a threaded executor relies on that, so keep them free of members.
  struct CopyJob {
    std::span<const uint8_t> bytes;
    WindowLease* dst;
    uint32_t num_channels;
  };

  static void swapBlocks(uint64_t& a, uint64_t& b, unsigned shift, uint64_t mask) {
    const uint64_t t = ((a >> shift) ^ b) & mask;
    a ^= t << shift;
    b ^= t;
  }
  static void transposeBytes8x8(std::array<uint64_t, 8>& x) {
    for (size_t i = 0; i < 4; ++i) swapBlocks(x[i], x[i + 4], 32, 0x00000000FFFFFFFFull);
    for (size_t i : {0, 1, 4, 5}) swapBlocks(x[i], x[i + 2], 16, 0x0000FFFF0000FFFFull);
    for (size_t i : {0, 2, 4, 6}) swapBlocks(x[i], x[i + 1], 8, 0x00FF00FF00FF00FFull);
  }

  static void store8(uint8_t* dst, uint64_t v) {
    if constexpr (std::endian::native == std::endian::little) {
      std::memcpy(dst, &v, 8);
    } else {
      for (unsigned k = 0; k < 8; ++k) dst[k] = static_cast<uint8_t>((v >> (8 * k)) & 0xFFu);
    }
  }

  template <unsigned kBytesPerSample>
  static void loadRowsAllLanes(const uint8_t* g, std::array<uint64_t, kBytesPerSample>& rows) {
    if constexpr (kBytesPerSample == 1 && std::endian::native == std::endian::little) {
      std::memcpy(&rows[0], g, 8);
    } else {
      // WHY: Byte loads, deliberately: native-integer loads plus shifts were tried for 2 and 4 bytes and measured slower.
      rows.fill(0);
      for (unsigned k = 0; k < 8; ++k) {
        const unsigned shift = 8 * k;
        const uint8_t* s = g + static_cast<size_t>(k) * kBytesPerSample;
        for (unsigned l = 0; l < kBytesPerSample; ++l) {
          rows[l] |= static_cast<uint64_t>(s[l]) << shift;
        }
      }
    }
  }

  template <unsigned kBytesPerSample>
  static void scalarStepsFixed(const CopyJob& job, uint32_t src_index, uint32_t dst_bit,
                               size_t steps) {
    static_assert(kBytesPerSample == 1 || kBytesPerSample == 2 || kBytesPerSample == 4);
    std::array<uint8_t*, 8 * kBytesPerSample> out{};
    for (uint32_t c = 0; c < job.num_channels; ++c) {
      out[c] = job.dst->channel(c).data() + dst_bit / 8;
    }
    const uint8_t* src = job.bytes.data() + static_cast<size_t>(src_index) * kBytesPerSample;
    for (size_t s = 0; s < steps; ++s, src += 64 * kBytesPerSample) {
      std::array<std::array<uint64_t, 8>, kBytesPerSample> x;
      for (unsigned j = 0; j < 8; ++j) {
        std::array<uint64_t, kBytesPerSample> rows;
        loadRowsAllLanes<kBytesPerSample>(src + static_cast<size_t>(8 * j) * kBytesPerSample, rows);
        for (unsigned l = 0; l < kBytesPerSample; ++l) x[l][j] = transpose8x8(rows[l]);
      }
      for (unsigned l = 0; l < kBytesPerSample; ++l) transposeBytes8x8(x[l]);
      for (uint32_t c = 0; c < job.num_channels; ++c) {
        store8(out[c] + s * 8, x[c / 8][c % 8]);
      }
    }
  }

  static void scalarStepsGeneric(const CopyJob& job, uint32_t src_index, uint32_t dst_bit,
                                 size_t steps) {
    const size_t bytes_per_sample = (job.num_channels + 7) / 8;
    for (uint32_t first = 0; first < job.num_channels; first += 8) {
      const uint32_t lane = first / 8;
      const uint32_t take = std::min<uint32_t>(8, job.num_channels - first);
      std::array<uint8_t*, 8> out{};
      for (uint32_t c = 0; c < take; ++c) {
        out[c] = job.dst->channel(first + c).data() + dst_bit / 8;
      }
      const uint8_t* src =
          job.bytes.data() + static_cast<size_t>(src_index) * bytes_per_sample + lane;
      for (size_t s = 0; s < steps; ++s, src += 64 * bytes_per_sample) {
        std::array<uint64_t, 8> x;
        for (unsigned j = 0; j < 8; ++j) {
          x[j] = transpose8x8(gatherRows(src + static_cast<size_t>(8 * j) * bytes_per_sample,
                                         bytes_per_sample));
        }
        transposeBytes8x8(x);
        for (uint32_t c = 0; c < take; ++c) store8(out[c] + s * 8, x[c]);
      }
    }
  }

  static void scalarSampleMajor(const CopyJob& job, uint32_t src_index, uint32_t dst_bit,
                                uint32_t count) {
    const uint32_t bytes_per_sample = (job.num_channels + 7) / 8;
    const size_t steps = count / 64;
    if (steps > 0) {
      if (bytes_per_sample == 1) {
        scalarStepsFixed<1>(job, src_index, dst_bit, steps);
      } else if (bytes_per_sample == 2) {
        scalarStepsFixed<2>(job, src_index, dst_bit, steps);
      } else if (bytes_per_sample == 4) {
        scalarStepsFixed<4>(job, src_index, dst_bit, steps);
      } else {
        scalarStepsGeneric(job, src_index, dst_bit, steps);
      }
    }
    const uint32_t done = static_cast<uint32_t>(steps) * 64;
    if (done < count) {
      scalarGroupSampleMajor(job, src_index + done, dst_bit + done, count - done);
    }
  }

  static void scalarGroupSampleMajor(const CopyJob& job, uint32_t src_index, uint32_t dst_bit,
                                     uint32_t count) {
    const size_t bytes_per_sample = (job.num_channels + 7) / 8;
    const size_t groups = count / 8;
    // WHY: Tile first, then copy out per channel: channels a power of two apart would evict each other's cache sets.
    constexpr size_t kBlockGroups = 64;
    std::array<uint8_t, 8 * kBlockGroups> tile;
    for (uint32_t first = 0; first < job.num_channels; first += 8) {
      const uint32_t lane = first / 8;
      const uint32_t take = std::min<uint32_t>(8, job.num_channels - first);
      std::array<uint8_t*, 8> out{};
      for (uint32_t c = 0; c < take; ++c) {
        out[c] = job.dst->channel(first + c).data() + dst_bit / 8;
      }
      const uint8_t* src = job.bytes.data() + static_cast<size_t>(src_index) * bytes_per_sample + lane;
      for (size_t base = 0; base < groups; base += kBlockGroups) {
        const size_t n = std::min(kBlockGroups, groups - base);
        for (size_t g = 0; g < n; ++g, src += 8 * bytes_per_sample) {
          uint64_t x = 0;
          if constexpr (std::endian::native == std::endian::little) {
            if (bytes_per_sample == 1) {
              std::memcpy(&x, src, 8);
            } else {
              x = gatherRows(src, bytes_per_sample);
            }
          } else {
            x = gatherRows(src, bytes_per_sample);
          }
          x = transpose8x8(x);
          for (uint32_t c = 0; c < take; ++c) {
            tile[c * kBlockGroups + g] = static_cast<uint8_t>((x >> (8 * c)) & 0xFFu);
          }
        }
        for (uint32_t c = 0; c < take; ++c) {
          std::memcpy(out[c] + base, &tile[c * kBlockGroups], n);
        }
      }
    }
  }

#if BITOSCOPE_WINDOW_ASSEMBLER_SSE2
  // WHY: Bit 7 is a lane's highest channel, so the ladder walks channels downwards; 16-bit shifts spill a bit into
  // the neighbour byte but cannot reach its bit 7 within the eight shifts.
  static void emitLane(__m128i v, uint32_t take, uint8_t* tile, size_t pitch, size_t p) {
    v = _mm_sll_epi16(v, _mm_cvtsi32_si128(static_cast<int>(8 - take)));
    for (uint32_t k = 0; k < take; ++k) {
      const uint16_t out = static_cast<uint16_t>(_mm_movemask_epi8(v));
      std::memcpy(tile + static_cast<size_t>(take - 1 - k) * pitch + 2 * p, &out, sizeof(out));
      v = _mm_slli_epi16(v, 1);
    }
  }


  // WHY: Stride 3 and above 4 stay scalar: extracting a stride-3 lane needs pshufb, which is SSSE3 and not baseline.
  template <unsigned kBytesPerSample>
  static void sseSampleMajor(const CopyJob& job, uint32_t src_index, uint32_t dst_bit,
                       size_t pairs) {
    static_assert(kBytesPerSample == 1 || kBytesPerSample == 2 || kBytesPerSample == 4,
                  "SSE2 can only split a lane out cheaply at a power-of-two stride");
    constexpr size_t kTileBytes = 8192;
    alignas(16) std::array<uint8_t, kTileBytes> tile;
    const size_t block_pairs = std::max<size_t>(1, kTileBytes / (2 * job.num_channels));
    const size_t pitch = 2 * block_pairs;

    const uint8_t* src = job.bytes.data() + static_cast<size_t>(src_index) * kBytesPerSample;
    for (size_t base = 0; base < pairs; base += block_pairs) {
      const size_t n = std::min(block_pairs, pairs - base);
      for (size_t p = 0; p < n; ++p, src += 16 * kBytesPerSample) {
        if constexpr (kBytesPerSample == 1) {
          emitLane(_mm_loadu_si128(reinterpret_cast<const __m128i*>(src)),
                   job.num_channels, tile.data(), pitch, p);
        } else if constexpr (kBytesPerSample == 2) {
          const __m128i low = _mm_set1_epi16(0x00FF);
          const __m128i a = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src));
          const __m128i b = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src + 16));
          emitLane(_mm_packus_epi16(_mm_and_si128(a, low), _mm_and_si128(b, low)), 8,
                   tile.data(), pitch, p);
          emitLane(_mm_packus_epi16(_mm_srli_epi16(a, 8), _mm_srli_epi16(b, 8)),
                   job.num_channels - 8, tile.data() + 8 * pitch, pitch, p);
        } else {
          const __m128i low = _mm_set1_epi32(0x000000FF);
          const __m128i a = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src));
          const __m128i b = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src + 16));
          const __m128i c = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src + 32));
          const __m128i d = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src + 48));
          for (uint32_t l = 0; l < 4; ++l) {
            const uint32_t take = std::min<uint32_t>(8, job.num_channels - 8 * l);
            if (take == 0) break;
            const int shift = static_cast<int>(8 * l);
            const __m128i pa = _mm_and_si128(_mm_srli_epi32(a, shift), low);
            const __m128i pb = _mm_and_si128(_mm_srli_epi32(b, shift), low);
            const __m128i pc = _mm_and_si128(_mm_srli_epi32(c, shift), low);
            const __m128i pd = _mm_and_si128(_mm_srli_epi32(d, shift), low);
            const __m128i v =
                _mm_packus_epi16(_mm_packs_epi32(pa, pb), _mm_packs_epi32(pc, pd));
            emitLane(v, take, tile.data() + static_cast<size_t>(l) * 8 * pitch,
                     pitch, p);
          }
        }
      }
      for (uint32_t ch = 0; ch < job.num_channels; ++ch) {
        std::memcpy(job.dst->channel(ch).data() + dst_bit / 8 + base * 2,
                    tile.data() + static_cast<size_t>(ch) * pitch, n * 2);
      }
    }
  }
#endif

  static void alignedSampleMajor(const CopyJob& job, uint32_t src_index, uint32_t dst_bit,
                           uint32_t count) {
    uint32_t consumed = 0;
#if BITOSCOPE_WINDOW_ASSEMBLER_SSE2
    const uint32_t bytes_per_sample = (job.num_channels + 7) / 8;
    if (count >= 16 && (bytes_per_sample == 1 || bytes_per_sample == 2 || bytes_per_sample == 4)) {
      const size_t pairs = count / 16;
      consumed = static_cast<uint32_t>(pairs) * 16;
      if (bytes_per_sample == 1) {
        sseSampleMajor<1>(job, src_index, dst_bit, pairs);
      } else if (bytes_per_sample == 2) {
        sseSampleMajor<2>(job, src_index, dst_bit, pairs);
      } else {
        sseSampleMajor<4>(job, src_index, dst_bit, pairs);
      }
    }
#endif
    if (consumed < count) {
      scalarSampleMajor(job, src_index + consumed, dst_bit + consumed, count - consumed);
    }
  }

  static uint64_t load8(const uint8_t* src) {
    uint64_t v = 0;
    if constexpr (std::endian::native == std::endian::little) {
      std::memcpy(&v, src, 8);
    } else {
      for (unsigned k = 0; k < 8; ++k) v |= static_cast<uint64_t>(src[k]) << (8 * k);
    }
    return v;
  }

  // WHY: A lane under 8 channels loads 8 bytes per row, up to 7 past it: rows whose load would cross payload end
  // go to the byte loop (safeRows).
  static constexpr size_t kChannelMajorTileSource = 8192;

 public:
  static constexpr size_t channelMajorTileBytes(uint32_t num_channels) {
    const size_t per_channel = kChannelMajorTileSource / (num_channels == 0 ? 1 : num_channels);
    return (std::min)(size_t{1024}, (std::max)(size_t{64}, per_channel / 16 * 16));
  }

 private:
  static void alignedChannelMajor(const CopyJob& job, uint32_t src_index, uint32_t dst_bit,
                                  uint32_t count) {
    const uint32_t n = job.num_channels;
    const size_t num_bytes = count / 8;
    const size_t dst_byte = dst_bit / 8;
    const uint8_t* src = job.bytes.data() + static_cast<size_t>(src_index / 8) * n;
    if (n == 1) {
      std::memcpy(job.dst->channel(0).data() + dst_byte, src, num_bytes);
      return;
    }
    const uint8_t* end = job.bytes.data() + job.bytes.size();
    const size_t tile = channelMajorTileBytes(n);
    static_assert(8 * channelMajorTileBytes(2) <= kChannelMajorStage);
    static_assert(16 * channelMajorTileBytes(16) <= kChannelMajorStage);
    alignas(64) std::array<uint8_t, kChannelMajorStage> stage;
    for (size_t t = 0; t < num_bytes; t += tile) {
      channelMajorTile(job, src + t * n, end, dst_byte + t, (std::min)(tile, num_bytes - t),
                       stage.data(), tile);
    }
  }
  static constexpr size_t kChannelMajorStage = 8192;

  static void channelMajorBytes(const uint8_t* src, uint32_t n, uint32_t first, uint32_t take,
                                uint8_t* stage, size_t pitch, size_t from, size_t to) {
    for (uint32_t j = 0; j < take; ++j) {
      uint8_t* out = stage + j * pitch;
      const uint8_t* s = src + first + j;
      for (size_t b = from; b < to; ++b) out[b] = s[b * n];
    }
  }

  static size_t safeRows(const uint8_t* src, const uint8_t* end, uint32_t n, uint32_t first,
                         size_t len, size_t block) {
    const size_t avail = static_cast<size_t>(end - src);
    const size_t need = size_t{first} + 8;
    const size_t rows = avail < need ? 0 : (avail - need) / n + 1;
    return (std::min)(len, rows) / block * block;
  }

  // WHY: Transpose into a stage, not the window: measured 1.4-1.6x faster at 16 and 32 channels (cache sets, inferred).
  static void channelMajorTile(const CopyJob& job, const uint8_t* src, const uint8_t* end,
                               size_t dst_byte, size_t len, uint8_t* stage, size_t pitch) {
    const uint32_t n = job.num_channels;
    const auto copyOut = [&](uint32_t first, uint32_t take) {
      for (uint32_t j = 0; j < take; ++j) {
        std::memcpy(job.dst->channel(first + j).data() + dst_byte, stage + j * pitch, len);
      }
    };
    uint32_t first = 0;
#if BITOSCOPE_WINDOW_ASSEMBLER_SSE2
    if (n == 2 || n == 4) {
      const size_t fast = len / 16 * 16;
      if (n == 2) {
        sseNarrow<2>(src, stage, pitch, fast);
      } else {
        sseNarrow<4>(src, stage, pitch, fast);
      }
      channelMajorBytes(src, n, 0, n, stage, pitch, fast, len);
      copyOut(0, n);
      return;
    }
    for (; n - first >= 16; first += 16) {
      const size_t fast = len / 16 * 16;
      sseLane16(src, n, first, stage, pitch, fast);
      channelMajorBytes(src, n, first, 16, stage, pitch, fast, len);
      copyOut(first, 16);
    }
    constexpr size_t kBlock = 16;
#else
    constexpr size_t kBlock = 8;
#endif
    for (; first < n; first += 8) {
      const uint32_t take = (std::min)(8u, n - first);
      size_t fast = safeRows(src, end, n, first, len, kBlock);
#if BITOSCOPE_WINDOW_ASSEMBLER_SSE2
      sseLane8(src, n, first, take, stage, pitch, fast);
#else
      fast = take == 8 ? fast : 0;
      scalarLane8(src, n, first, stage, pitch, fast);
#endif
      channelMajorBytes(src, n, first, take, stage, pitch, fast, len);
      copyOut(first, take);
    }
  }

  // WHY: Eight named words, not an array: MSVC keeps them in registers where it spilled the array.
  static void scalarLane8(const uint8_t* src, uint32_t n, uint32_t first, uint8_t* stage,
                          size_t pitch, size_t rows) {
    constexpr uint64_t k32 = 0x00000000FFFFFFFFull;
    constexpr uint64_t k16 = 0x0000FFFF0000FFFFull;
    constexpr uint64_t k8 = 0x00FF00FF00FF00FFull;
    for (size_t b = 0; b < rows; b += 8) {
      const uint8_t* s = src + b * n + first;
      uint64_t x0 = load8(s);
      uint64_t x1 = load8(s + size_t{n});
      uint64_t x2 = load8(s + 2 * size_t{n});
      uint64_t x3 = load8(s + 3 * size_t{n});
      uint64_t x4 = load8(s + 4 * size_t{n});
      uint64_t x5 = load8(s + 5 * size_t{n});
      uint64_t x6 = load8(s + 6 * size_t{n});
      uint64_t x7 = load8(s + 7 * size_t{n});
      swapBlocks(x0, x4, 32, k32);
      swapBlocks(x1, x5, 32, k32);
      swapBlocks(x2, x6, 32, k32);
      swapBlocks(x3, x7, 32, k32);
      swapBlocks(x0, x2, 16, k16);
      swapBlocks(x1, x3, 16, k16);
      swapBlocks(x4, x6, 16, k16);
      swapBlocks(x5, x7, 16, k16);
      swapBlocks(x0, x1, 8, k8);
      swapBlocks(x2, x3, 8, k8);
      swapBlocks(x4, x5, 8, k8);
      swapBlocks(x6, x7, 8, k8);
      store8(stage + b, x0);
      store8(stage + pitch + b, x1);
      store8(stage + 2 * pitch + b, x2);
      store8(stage + 3 * pitch + b, x3);
      store8(stage + 4 * pitch + b, x4);
      store8(stage + 5 * pitch + b, x5);
      store8(stage + 6 * pitch + b, x6);
      store8(stage + 7 * pitch + b, x7);
    }
  }

#if BITOSCOPE_WINDOW_ASSEMBLER_SSE2
  struct Xmm8 {
    __m128i reg[8];
    __m128i& operator[](size_t i) { return reg[i]; }
  };

  static void finishTranspose8(Xmm8& y) {
    Xmm8 b;
    for (unsigned q = 0; q < 4; ++q) {
      b[q] = _mm_unpacklo_epi16(y[2 * q], y[2 * q + 1]);
      b[4 + q] = _mm_unpackhi_epi16(y[2 * q], y[2 * q + 1]);
    }
    Xmm8 a;
    for (unsigned h = 0; h < 2; ++h) {
      for (unsigned r = 0; r < 2; ++r) {
        a[4 * h + r] = _mm_unpacklo_epi32(b[4 * h + 2 * r], b[4 * h + 2 * r + 1]);
        a[4 * h + 2 + r] = _mm_unpackhi_epi32(b[4 * h + 2 * r], b[4 * h + 2 * r + 1]);
      }
    }
    for (unsigned m = 0; m < 4; ++m) {
      y[2 * m] = _mm_unpacklo_epi64(a[2 * m], a[2 * m + 1]);
      y[2 * m + 1] = _mm_unpackhi_epi64(a[2 * m], a[2 * m + 1]);
    }
  }

  static void sseLane8(const uint8_t* src, uint32_t n, uint32_t first, uint32_t take,
                       uint8_t* stage, size_t pitch, size_t rows) {
    for (size_t b = 0; b < rows; b += 16) {
      const uint8_t* s = src + b * n + first;
      Xmm8 y;
      for (unsigned p = 0; p < 8; ++p) {
        y[p] = _mm_unpacklo_epi8(
            _mm_loadl_epi64(reinterpret_cast<const __m128i*>(s + (2 * p) * size_t{n})),
            _mm_loadl_epi64(reinterpret_cast<const __m128i*>(s + (2 * p + 1) * size_t{n})));
      }
      finishTranspose8(y);
      for (uint32_t j = 0; j < take; ++j) {
        _mm_storeu_si128(reinterpret_cast<__m128i*>(stage + j * pitch + b), y[j]);
      }
    }
  }

  template <uint32_t kChannels>
  static void sseNarrow(const uint8_t* src, uint8_t* stage, size_t pitch, size_t rows) {
    static_assert(kChannels == 2 || kChannels == 4);
    for (size_t b = 0; b < rows; b += 16) {
      const uint8_t* s = src + b * kChannels;
      const __m128i a = _mm_loadu_si128(reinterpret_cast<const __m128i*>(s));
      const __m128i c = _mm_loadu_si128(reinterpret_cast<const __m128i*>(s + 16));
      if constexpr (kChannels == 2) {
        const __m128i low = _mm_set1_epi16(0x00FF);
        _mm_storeu_si128(reinterpret_cast<__m128i*>(stage + b),
                         _mm_packus_epi16(_mm_and_si128(a, low), _mm_and_si128(c, low)));
        _mm_storeu_si128(reinterpret_cast<__m128i*>(stage + pitch + b),
                         _mm_packus_epi16(_mm_srli_epi16(a, 8), _mm_srli_epi16(c, 8)));
      } else {
        const __m128i d = _mm_loadu_si128(reinterpret_cast<const __m128i*>(s + 32));
        const __m128i e = _mm_loadu_si128(reinterpret_cast<const __m128i*>(s + 48));
        const __m128i low = _mm_set1_epi32(0x000000FF);
        for (uint32_t j = 0; j < 4; ++j) {
          const __m128i shift = _mm_cvtsi32_si128(static_cast<int>(8 * j));
          const __m128i pa = _mm_and_si128(_mm_srl_epi32(a, shift), low);
          const __m128i pc = _mm_and_si128(_mm_srl_epi32(c, shift), low);
          const __m128i pd = _mm_and_si128(_mm_srl_epi32(d, shift), low);
          const __m128i pe = _mm_and_si128(_mm_srl_epi32(e, shift), low);
          _mm_storeu_si128(reinterpret_cast<__m128i*>(stage + j * pitch + b),
                           _mm_packus_epi16(_mm_packs_epi32(pa, pc), _mm_packs_epi32(pd, pe)));
        }
      }
    }
  }

  static void sseLane16(const uint8_t* src, uint32_t n, uint32_t first, uint8_t* stage,
                        size_t pitch, size_t rows) {
    for (size_t b = 0; b < rows; b += 16) {
      const uint8_t* s = src + b * n + first;
      Xmm8 lo;
      Xmm8 hi;
      for (unsigned p = 0; p < 8; ++p) {
        const __m128i r0 =
            _mm_loadu_si128(reinterpret_cast<const __m128i*>(s + (2 * p) * size_t{n}));
        const __m128i r1 =
            _mm_loadu_si128(reinterpret_cast<const __m128i*>(s + (2 * p + 1) * size_t{n}));
        lo[p] = _mm_unpacklo_epi8(r0, r1);
        hi[p] = _mm_unpackhi_epi8(r0, r1);
      }
      finishTranspose8(lo);
      finishTranspose8(hi);
      for (uint32_t j = 0; j < 8; ++j) {
        _mm_storeu_si128(reinterpret_cast<__m128i*>(stage + j * pitch + b), lo[j]);
        _mm_storeu_si128(reinterpret_cast<__m128i*>(stage + (8 + j) * pitch + b), hi[j]);
      }
    }
  }
#endif

  struct SliceJob {
    const CopyJob* job;
    uint32_t src_index;
    uint32_t dst_bit;
    uint32_t body;
    uint32_t slice_samples;
    bool sample_major;
  };

  static void runSlice(void* ctx, size_t slice) {
    const SliceJob& s = *static_cast<const SliceJob*>(ctx);
    const SliceRange r = SlicePlan::range(slice, s.body, s.slice_samples);
    if (s.sample_major) {
      alignedSampleMajor(*s.job, s.src_index + r.first, s.dst_bit + r.first, r.count);
    } else {
      alignedChannelMajor(*s.job, s.src_index + r.first, s.dst_bit + r.first, r.count);
    }
  }

  void copyAligned(const CopyJob& job, bool sample_major, uint32_t src_index, uint32_t dst_bit,
                   uint32_t body) {
    const size_t slices = SlicePlan::count(body, config_.parallel_slice_samples);
    if (slices <= 1) {
      if (sample_major) {
        alignedSampleMajor(job, src_index, dst_bit, body);
      } else {
        alignedChannelMajor(job, src_index, dst_bit, body);
      }
      return;
    }
    SliceJob slice_job{&job, src_index, dst_bit, body, config_.parallel_slice_samples, sample_major};
    CopyExecutor& executor = config_.executor != nullptr ? *config_.executor : inline_executor_;
    executor.run(slices, &WindowAssembler::runSlice, &slice_job);
  }

  // WHY: Split off a head of up to 7 samples: filled_ never returns to a byte boundary by itself, so slow path stays.
  void copySampleMajor(std::span<const uint8_t> bytes, uint32_t chunk_offset, uint32_t count) {
    uint32_t done = 0;
    if (filled_ % 8 != 0) {
      const uint32_t head = std::min(8 - filled_ % 8, count);
      sampleMajorBits(bytes, chunk_offset, filled_, head);
      done = head;
    }
    const uint32_t body = ((count - done) / 8) * 8;
    if (body > 0) {
      const CopyJob job{bytes, &*current_, num_channels_};
      constexpr bool sampleMajorInput = true;
      copyAligned(job, sampleMajorInput, chunk_offset + done, filled_ + done, body);
      done += body;
    }
    if (done < count) {
      sampleMajorBits(bytes, chunk_offset + done, filled_ + done, count - done);
    }
  }

  void sampleMajorBits(std::span<const uint8_t> bytes, uint32_t src_index, uint32_t dst_bit,
                        uint32_t count) {
    const size_t bytes_per_sample = (num_channels_ + 7) / 8;
    for (uint32_t ch = 0; ch < num_channels_; ++ch) {
      const std::span<uint8_t> dst = current_->channel(ch);
      for (uint32_t k = 0; k < count; ++k) {
        const size_t offset =
            static_cast<size_t>(src_index + k) * bytes_per_sample + ch / 8;
        const uint32_t bit = dst_bit + k;
        if (((bytes[offset] >> (ch % 8)) & 1) != 0) {
          dst[bit / 8] |= static_cast<uint8_t>(1u << (bit % 8));
        } else {
          dst[bit / 8] &= static_cast<uint8_t>(~(1u << (bit % 8)));
        }
      }
    }
  }

  void copySamples(std::span<const uint8_t> bytes, SampleEncoding encoding,
                    uint32_t chunk_offset, uint32_t count) {
    if (encoding == SampleEncoding::SampleMajorPacked) {
      copySampleMajor(bytes, chunk_offset, count);
      return;
    }
    if (chunk_offset % 8 == 0 && filled_ % 8 == 0 && count % 8 == 0) {
      const CopyJob job{bytes, &*current_, num_channels_};
      constexpr bool sampleMajorInput = false;
      copyAligned(job, sampleMajorInput, chunk_offset, filled_, count);
      return;
    }

    for (uint32_t ch = 0; ch < num_channels_; ++ch) {
      const std::span<uint8_t> dst = current_->channel(ch);
      for (uint32_t k = 0; k < count; ++k) {
        const uint32_t index = chunk_offset + k;
        const size_t offset = static_cast<size_t>(index / 8) * num_channels_ + ch;
        const uint32_t bit = filled_ + k;
        if (((bytes[offset] >> (index % 8)) & 1) != 0) {
          dst[bit / 8] |= static_cast<uint8_t>(1u << (bit % 8));
        } else {
          dst[bit / 8] &= static_cast<uint8_t>(~(1u << (bit % 8)));
        }
      }
    }
  }

  AssemblerConfig config_;
  uint32_t num_channels_;
  std::unique_ptr<WindowPool> pool_;
  InlineCopyExecutor inline_executor_;
  std::optional<WindowLease> current_;
  uint32_t filled_ = 0;
  uint64_t window_first_sample_ = 0;
  uint64_t sample_rate_milli_hz_ = 0;
  uint64_t gap_pending_ = 0;
  uint64_t gap_total_ = 0;
  bool started_ = false;
  uint64_t expected_next_index_ = 0;
  WaitLimit wait_limit_;
  std::optional<WaitEnd> wait_ended_;
};

}
