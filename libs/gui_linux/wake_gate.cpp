/*
 * SPDX-License-Identifier: MIT
 */

#include "wake_gate.h"

#include <utility>

namespace bitoscope::gui_linux {

WakeGate::WakeGate(std::function<void()> wake) : wake_(std::move(wake)) {}

void WakeGate::open() {
  std::lock_guard lock(mutex_);
  open_ = true;
}

void WakeGate::close() {
  // WHY: Taking the lock waits for a wake() that is calling the wake function now.
  std::lock_guard lock(mutex_);
  open_ = false;
}

void WakeGate::wake() {
  std::lock_guard lock(mutex_);
  if (open_ && wake_) {
    wake_();
  }
}

}
