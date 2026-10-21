/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <memory>
#include <mutex>

#include "capture_session.h"

namespace bitoscope::gui_capture {

class LiveCaptureSlot {
 public:
  void set(std::shared_ptr<bitoscope::capture::CaptureSession> session);

  class View {
   public:
    const std::shared_ptr<bitoscope::capture::CaptureSession>& session() const { return session_; }
    uint64_t generation() const { return generation_; }

   private:
    friend class LiveCaptureSlot;
    View(std::unique_lock<std::mutex> lock, std::shared_ptr<bitoscope::capture::CaptureSession> session,
         uint64_t generation)
        : lock_(std::move(lock)), session_(std::move(session)), generation_(generation) {}
    std::unique_lock<std::mutex> lock_;
    std::shared_ptr<bitoscope::capture::CaptureSession> session_;
    uint64_t generation_ = 0;
  };

  View view();

 private:
  std::mutex mutex_;
  std::shared_ptr<bitoscope::capture::CaptureSession> session_;
  uint64_t generation_ = 0;
};

}
