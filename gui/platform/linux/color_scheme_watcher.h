/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <color_scheme.h>
#include <color_scheme_state.h>

#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace bitoscope::gui::linux_platform {

class ColorSchemeWatcher {
 public:
  explicit ColorSchemeWatcher(std::function<void()> onChange);
  ~ColorSchemeWatcher();
  ColorSchemeWatcher(const ColorSchemeWatcher&) = delete;
  ColorSchemeWatcher& operator=(const ColorSchemeWatcher&) = delete;

  gui_linux::ColorScheme current();

  std::vector<std::string> takeNotes();

  static constexpr int kFirstAnswerWaitMilliseconds = 1500;

 private:
  void run();
  void publish(gui_linux::ColorScheme scheme, bool first);
  void note(std::string line);

  std::function<void()> onChange_;
  int stopEvent_ = -1;

  std::mutex mutex_;
  std::condition_variable answered_;
  gui_linux::ColorSchemeState state_;
  std::vector<std::string> notes_;

  std::thread thread_;  // WHY: thread_ must stay the last member: it starts once every other member exists.
};

}
