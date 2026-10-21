/*
 * SPDX-License-Identifier: MIT
 */

#include "live_capture_slot.h"

#include <utility>

namespace bitoscope::gui_capture {

void LiveCaptureSlot::set(std::shared_ptr<bitoscope::capture::CaptureSession> session) {
  std::shared_ptr<bitoscope::capture::CaptureSession> previous;
  {
    std::lock_guard lock(mutex_);
    previous = std::exchange(session_, std::move(session));
    ++generation_;
  }
  // WHY: Destroy `previous` outside the lock: session destruction joins pack workers and must not hold up the UI's next View.
}

LiveCaptureSlot::View LiveCaptureSlot::view() {
  std::unique_lock lock(mutex_);
  auto session = session_;
  const uint64_t generation = generation_;
  return View(std::move(lock), std::move(session), generation);
}

}
