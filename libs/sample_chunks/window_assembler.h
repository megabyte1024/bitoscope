/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>

#include "chunk_view.h"
#include "sample_encoding.h"
#include "sample_window.h"
#include "window_assembler_kernels.h"

namespace bitoscope::host {

struct AssemblerConfig {
  uint32_t samples_per_window = 0;
  uint32_t num_buffers = 0;
};

class WindowAssembler {
 public:
  using WindowFn = std::function<void(WindowLease)>;

  static std::expected<WindowAssembler, std::string> create(AssemblerConfig config,
      uint32_t num_channels);

  WindowAssembler(WindowAssembler&&) = default;
  WindowAssembler(const WindowAssembler&) = delete;
  WindowAssembler& operator=(const WindowAssembler&) = delete;

  std::expected<void, std::string> submit(const ChunkView& chunk, const WindowFn& on_window);

  void flush(const WindowFn& on_window);

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

  static constexpr size_t channelMajorTileBytes(uint32_t num_channels) {
    return assembler_kernels::channelMajorTileBytes(num_channels);
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

  void beginWindow() {
    current_.reset();
    filled_ = 0;
  }

  void zeroTail();

  void emit(const WindowFn& on_window);

  void copySampleMajor(std::span<const uint8_t> bytes, uint32_t chunk_offset, uint32_t count);

  void sampleMajorBits(std::span<const uint8_t> bytes, uint32_t src_index, uint32_t dst_bit,
      uint32_t count);

  void copySamples(std::span<const uint8_t> bytes, SampleEncoding encoding, uint32_t chunk_offset,
      uint32_t count);

  AssemblerConfig config_;
  uint32_t num_channels_;
  std::unique_ptr<WindowPool> pool_;
  std::optional<WindowLease> current_;
  uint32_t filled_ = 0;
  uint64_t window_first_sample_ = 0;
  uint64_t sample_rate_milli_hz_ = 0;
  bool started_ = false;
  uint64_t expected_next_index_ = 0;
  WaitLimit wait_limit_;
  std::optional<WaitEnd> wait_ended_;
};

}
