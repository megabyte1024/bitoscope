/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <expected>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>

#include "capture_extent.h"
#include "capture_plan.h"
#include "pyramid.h"
#include "raw_capture_buffer.h"

namespace bitoscope::capture {
class CaptureSession;
}

namespace bitoscope::decoder_host {

inline constexpr std::chrono::milliseconds kGrowingFeedWait{2};

class GrowingCapture {
 public:
  virtual ~GrowingCapture() = default;
  virtual const capture::CapturePlan& plan() const = 0;
  virtual bool hasPyramid() const = 0;
  virtual capture::CaptureExtent publishedExtent() const = 0;
  virtual std::span<const capture::IndexRecord> indexRange(uint64_t first, uint64_t end) const = 0;
  virtual std::expected<void, std::string> read(uint32_t channel, uint64_t first_byte, std::span<uint8_t> out) const = 0;
  virtual std::expected<void, std::string> pyramidSummary(uint32_t channel, uint64_t begin, uint64_t end,
                                                          std::span<capture::Bucket> out) const = 0;
};

std::shared_ptr<const GrowingCapture> sessionCapture(std::shared_ptr<const capture::CaptureSession> session);

class CaptureEnd {
 public:
  CaptureEnd() = default;
  explicit CaptureEnd(capture::CaptureExtent stored);

  void keep(capture::CaptureExtent stored);
  std::optional<capture::CaptureExtent> stored() const;
  std::optional<capture::CaptureExtent> waitForStored(std::chrono::milliseconds wait) const;

 private:
  mutable std::mutex mutex_;
  mutable std::condition_variable kept_;
  std::optional<capture::CaptureExtent> stored_;
};

}
