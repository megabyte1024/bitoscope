/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "test_command.h"

namespace bitoscope::gui_test_input {

class CommandChannel {
 public:
  struct Pending {
    std::uint64_t ticket = 0;
    Command command;
  };

  std::string submitAndWait(Command command);

  std::vector<Pending> take();

  void reply(std::uint64_t ticket, std::string text);

  void close();

  void setWakeCallback(std::function<void()> callback);

 private:
  std::mutex mutex_;
  std::condition_variable replied_;
  std::vector<Pending> incoming_;
  std::set<std::uint64_t> waiting_;
  std::map<std::uint64_t, std::string> answers_;
  std::uint64_t nextTicket_ = 1;
  bool closed_ = false;
  std::function<void()> wake_;
};

}
