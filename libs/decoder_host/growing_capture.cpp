/*
 * SPDX-License-Identifier: MIT
 */

#include "growing_capture.h"

#include <utility>

#include "capture_session.h"

namespace bitoscope::decoder_host {

namespace {

class SessionCapture final : public GrowingCapture {
 public:
  explicit SessionCapture(std::shared_ptr<const capture::CaptureSession> session) : session_(std::move(session)) {}

  const capture::CapturePlan& plan() const override { return session_->buffer().plan(); }
  bool hasPyramid() const override { return session_->buffer().hasPyramid(); }
  capture::CaptureExtent publishedExtent() const override { return session_->snapshot().extent; }
  std::span<const capture::IndexRecord> indexRange(uint64_t first, uint64_t end) const override {
    return session_->buffer().indexRange(first, end);
  }
  std::expected<void, std::string> read(uint32_t channel, uint64_t first_byte, std::span<uint8_t> out) const override {
    return session_->buffer().read(channel, first_byte, out);
  }
  std::expected<void, std::string> pyramidSummary(uint32_t channel, uint64_t begin, uint64_t end,
                                                  std::span<capture::Bucket> out) const override {
    return session_->buffer().pyramidSummary(channel, begin, end, out);
  }

 private:
  std::shared_ptr<const capture::CaptureSession> session_;
};

}

std::shared_ptr<const GrowingCapture> sessionCapture(std::shared_ptr<const capture::CaptureSession> session) {
  return std::make_shared<const SessionCapture>(std::move(session));
}

CaptureEnd::CaptureEnd(capture::CaptureExtent stored) : stored_(stored) {}

void CaptureEnd::keep(capture::CaptureExtent stored) {
  {
    std::lock_guard lock(mutex_);
    if (stored_) return;
    stored_ = stored;
  }
  kept_.notify_all();
}

std::optional<capture::CaptureExtent> CaptureEnd::stored() const {
  std::lock_guard lock(mutex_);
  return stored_;
}

std::optional<capture::CaptureExtent> CaptureEnd::waitForStored(std::chrono::milliseconds wait) const {
  std::unique_lock lock(mutex_);
  kept_.wait_for(lock, wait, [this] { return stored_.has_value(); });
  return stored_;
}

}
