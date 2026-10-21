/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

// WHY: The wake function runs with the gate's lock held: it must be quick and must not call back into the gate,
// and close() must return only once no call runs so glfwTerminate() cannot race one.

#include <functional>
#include <mutex>

namespace bitoscope::gui_linux {

class WakeGate {
 public:
  explicit WakeGate(std::function<void()> wake);
  WakeGate(const WakeGate&) = delete;
  WakeGate& operator=(const WakeGate&) = delete;

  void open();
  void close();
  void wake();

 private:
  std::mutex mutex_;
  bool open_ = false;
  std::function<void()> wake_;
};

}
