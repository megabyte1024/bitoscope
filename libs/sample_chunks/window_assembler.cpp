/*
 * SPDX-License-Identifier: MIT
 */

#include "window_assembler.h"

#include <algorithm>
#include <cstring>

namespace bitoscope::host {

std::expected<WindowAssembler, std::string> WindowAssembler::create(AssemblerConfig config,
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
  return WindowAssembler(config, num_channels);
}

std::expected<void, std::string> WindowAssembler::submit(const ChunkView& chunk,
    const WindowFn& on_window) {
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
    if (chunk.first_sample_index > expected_next_index_) {
      return std::unexpected("chunk starts at sample " + std::to_string(chunk.first_sample_index) +
                              " but next expected is " + std::to_string(expected_next_index_) + " - samples " +
                              std::to_string(expected_next_index_) + " to " +
                              std::to_string(chunk.first_sample_index - 1) + " (" +
                              std::to_string(chunk.first_sample_index - expected_next_index_) +
                              " samples) never arrived");
    }
  }

  if (!started_) {
    started_ = true;
    sample_rate_milli_hz_ = chunk.sample_rate_milli_hz;
    window_first_sample_ = chunk.first_sample_index;
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

void WindowAssembler::flush(const WindowFn& on_window) {
  if (filled_ > 0) {
    emit(on_window);
  }
}

// WHY: acquire() does not clear a slot: a partial window would leak an earlier window's bits past num_samples.
void WindowAssembler::zeroTail() {
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

void WindowAssembler::emit(const WindowFn& on_window) {
  zeroTail();
  WindowInfo& info = current_->info();
  info.first_sample_index = window_first_sample_;
  info.sample_rate_milli_hz = sample_rate_milli_hz_;
  info.num_channels = num_channels_;
  info.num_samples = filled_;
  on_window(std::move(*current_));
  window_first_sample_ += filled_;
  beginWindow();
}

// WHY: Split off a head of up to 7 samples: filled_ never returns to a byte boundary by itself, so slow path stays.
void WindowAssembler::copySampleMajor(std::span<const uint8_t> bytes, uint32_t chunk_offset,
    uint32_t count) {
  uint32_t done = 0;
  if (filled_ % 8 != 0) {
    const uint32_t head = std::min(8 - filled_ % 8, count);
    sampleMajorBits(bytes, chunk_offset, filled_, head);
    done = head;
  }
  const uint32_t body = ((count - done) / 8) * 8;
  if (body > 0) {
    const assembler_kernels::CopyJob job{bytes, &*current_, num_channels_};
    assembler_kernels::alignedSampleMajor(job, chunk_offset + done, filled_ + done, body);
    done += body;
  }
  if (done < count) {
    sampleMajorBits(bytes, chunk_offset + done, filled_ + done, count - done);
  }
}

void WindowAssembler::sampleMajorBits(std::span<const uint8_t> bytes, uint32_t src_index,
    uint32_t dst_bit, uint32_t count) {
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

void WindowAssembler::copySamples(std::span<const uint8_t> bytes, SampleEncoding encoding,
    uint32_t chunk_offset, uint32_t count) {
  if (encoding == SampleEncoding::SampleMajorPacked) {
    copySampleMajor(bytes, chunk_offset, count);
    return;
  }
  if (chunk_offset % 8 == 0 && filled_ % 8 == 0 && count % 8 == 0) {
    const assembler_kernels::CopyJob job{bytes, &*current_, num_channels_};
    assembler_kernels::alignedChannelMajor(job, chunk_offset, filled_, count);
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

}
