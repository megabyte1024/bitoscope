/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <sys/types.h>

#include <atomic>
#include <expected>
#include <string>
#include <thread>
#include <vector>

#include <command_channel.h>

namespace bitoscope::gui_test_socket {

std::expected<void, std::string> ensurePrivateDirectory(const std::string& directory, uid_t uid);

class TestSocketServer {
 public:
  TestSocketServer(std::string path, bitoscope::gui_test_input::CommandChannel& channel, uid_t allowedUid);
  ~TestSocketServer();
  TestSocketServer(const TestSocketServer&) = delete;
  TestSocketServer& operator=(const TestSocketServer&) = delete;

  std::expected<void, std::string> start();

  bool replacedStaleSocket() const { return replacedStale_; }

  std::expected<std::vector<int>, std::string> removeFileOnTerminationSignal();

  void stop();

 private:
  void run();
  void serve(int client);
  void turnAway(int client);
  bool peerAllowed(int client) const;

  std::string path_;
  bitoscope::gui_test_input::CommandChannel& channel_;
  uid_t allowedUid_;
  int listenFd_ = -1;
  int stopRead_ = -1;
  int stopWrite_ = -1;
  dev_t boundDevice_ = 0;
  ino_t boundInode_ = 0;
  bool bound_ = false;
  bool replacedStale_ = false;
  bool removalArmed_ = false;
  std::thread thread_;
};

}
