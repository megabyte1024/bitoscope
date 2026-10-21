/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <atomic>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "capture_extent.h"

namespace bitoscope::host {
class WindowLease;
}

namespace bitoscope::capture {

struct CaptureSnapshot {
  uint64_t generation = 0;
  CaptureExtent extent;
  std::optional<uint64_t> trigger_origin;
  std::optional<uint64_t> trigger_sample_index;

  bool operator==(const CaptureSnapshot&) const = default;
};

struct CaptureTail {
  uint64_t generation = 0;
  uint64_t first_sample = 0;
  uint64_t count = 0;
  uint32_t channels = 0;
  uint64_t words_per_channel = 0;
  std::vector<uint64_t> words;

  bool level(uint32_t channel, uint64_t sample) const;
  uint64_t word(uint32_t channel, uint64_t index) const;
};

class PublishedCaptureSnapshot {
 public:
  std::expected<void, std::string> reserveTail(uint32_t channels, uint32_t window_samples);

  void publish(CaptureExtent extent, std::optional<uint64_t> trigger_origin,
               std::optional<uint64_t> trigger_sample_index);

  bool publishTail(CaptureExtent extent, std::optional<uint64_t> trigger_origin,
                   std::optional<uint64_t> trigger_sample_index, const bitoscope::host::WindowLease* window,
                   uint64_t tail_samples);

  CaptureSnapshot read() const;

  CaptureSnapshot read(CaptureTail& tail) const;

 private:
  bool holds(CaptureExtent extent, uint8_t present, uint64_t origin, uint64_t index, uint64_t tail_samples) const;
  bool store(CaptureExtent extent, std::optional<uint64_t> trigger_origin,
             std::optional<uint64_t> trigger_sample_index, const bitoscope::host::WindowLease* window,
             uint64_t tail_samples);
  CaptureSnapshot readInto(CaptureTail* tail) const;
  void copyTail(CaptureTail& tail, uint64_t tail_samples) const;

  std::atomic<uint64_t> sequence_{0};
  std::atomic<uint64_t> first_sample_{0};
  std::atomic<uint64_t> count_{0};
  std::atomic<uint64_t> trigger_origin_{0};
  std::atomic<uint64_t> trigger_sample_index_{0};
  std::atomic<uint8_t> present_{0};
  std::atomic<uint64_t> tail_samples_{0};
  uint32_t tail_channels_ = 0;
  uint64_t tail_capacity_ = 0;
  uint64_t tail_stride_ = 0;
  std::unique_ptr<std::atomic<uint64_t>[]> tail_words_;
};

}
