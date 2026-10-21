/*
 * SPDX-License-Identifier: MIT
 */

#include "command_channel.h"

#include <utility>

namespace bitoscope::gui_test_input {

std::string CommandChannel::submitAndWait(Command command) {
  std::uint64_t ticket = 0;
  std::function<void()> wake;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (closed_) {
      return "error shutting down";
    }
    ticket = nextTicket_++;
    waiting_.insert(ticket);
    incoming_.push_back(Pending{ticket, std::move(command)});
    wake = wake_;
  }
  if (wake) {
    wake();  // WHY: Call outside the lock: the callback may call take() or reply().
  }
  std::unique_lock<std::mutex> lock(mutex_);
  replied_.wait(lock, [&] { return closed_ || answers_.contains(ticket); });
  waiting_.erase(ticket);
  const auto answer = answers_.find(ticket);
  if (answer == answers_.end()) {
    return "error shutting down";
  }
  std::string text = std::move(answer->second);
  answers_.erase(answer);
  return text;
}

std::vector<CommandChannel::Pending> CommandChannel::take() {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<Pending> taken;
  taken.swap(incoming_);
  return taken;
}

void CommandChannel::reply(std::uint64_t ticket, std::string text) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!waiting_.contains(ticket)) {
      return;
    }
    answers_[ticket] = std::move(text);
  }
  replied_.notify_all();
}

void CommandChannel::close() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    closed_ = true;
    incoming_.clear();
  }
  replied_.notify_all();
}

void CommandChannel::setWakeCallback(std::function<void()> callback) {
  std::lock_guard<std::mutex> lock(mutex_);
  wake_ = std::move(callback);
}

}
