/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#ifdef BITOSCOPE_GUI_TEST_HOOK

#include <windows.h>

#include <atomic>
#include <string>
#include <thread>

#include <command_channel.h>

namespace bitoscope::gui::win32 {

class TestPipeServer {
 public:
  TestPipeServer(std::string pipeName, bitoscope::gui_test_input::CommandChannel& channel);
  ~TestPipeServer();
  TestPipeServer(const TestPipeServer&) = delete;
  TestPipeServer& operator=(const TestPipeServer&) = delete;

  bool start(std::string& error);

  void stop();

 private:
  void run();
  void serve();
  bool writeLine(const std::string& line);

  std::string pipeName_;
  bitoscope::gui_test_input::CommandChannel& channel_;
  HANDLE pipe_ = INVALID_HANDLE_VALUE;
  std::thread thread_;
  std::atomic<bool> stopping_{false};
  std::atomic<bool> finished_{false};
  std::atomic<bool> replying_{false};
};

}

#endif
