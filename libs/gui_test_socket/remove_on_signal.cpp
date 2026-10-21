/*
 * SPDX-License-Identifier: MIT
 */

#include "remove_on_signal.h"

#include <signal.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstring>
#include <format>
#include <mutex>

namespace bitoscope::gui_test_socket {

namespace {

constexpr std::array<int, 3> kSignals{SIGTERM, SIGINT, SIGHUP};

// WHY: Written only while no handler of ours is installed, so the handler never sees a half-written path.
char removalPath[kMaxRemovalPath + 1] = {};

struct sigaction previous[kSignals.size()] = {};
bool installed[kSignals.size()] = {};
bool armed = false;
std::mutex armMutex;  // WHY: arm and disarm only; never taken by the signal handler

// WHY: Handler may call only async-signal-safe functions (unlink, sigaction, raise); no allocation, no streams.
void removeAndEnd(int signal) {
  const int savedErrno = errno;
  unlink(removalPath);
  struct sigaction fallback {};
  fallback.sa_handler = SIG_DFL;
  sigemptyset(&fallback.sa_mask);
  sigaction(signal, &fallback, nullptr);
  raise(signal);
  errno = savedErrno;
}

void restoreLocked() {
  for (std::size_t i = 0; i < kSignals.size(); ++i) {
    if (installed[i]) {
      sigaction(kSignals[i], &previous[i], nullptr);
      installed[i] = false;
    }
  }
  armed = false;
}

}

std::expected<std::vector<int>, std::string> armFileRemovalOnSignal(std::string_view path) {
  std::lock_guard lock(armMutex);
  if (armed) {
    return std::unexpected(std::format("already armed for {}", removalPath));
  }
  if (path.empty()) {
    return std::unexpected(std::string("no path to remove"));
  }
  if (path.size() > kMaxRemovalPath) {
    return std::unexpected(std::format("path of {} bytes is longer than {}", path.size(), kMaxRemovalPath));
  }
  std::memcpy(removalPath, path.data(), path.size());
  removalPath[path.size()] = '\0';
  armed = true;
  std::vector<int> handled;
  for (std::size_t i = 0; i < kSignals.size(); ++i) {
    struct sigaction now {};
    if (sigaction(kSignals[i], nullptr, &now) != 0) {
      const int error = errno;
      restoreLocked();
      return std::unexpected(std::format("sigaction({}) could not be read: {}", kSignals[i], std::strerror(error)));
    }
    const bool isDefault = (now.sa_flags & SA_SIGINFO) == 0 && now.sa_handler == SIG_DFL;
    if (!isDefault) {
      continue;  // WHY: Signal ignored or handled elsewhere is left alone: removing the file under a live server breaks the hook.
    }
    struct sigaction ours {};
    ours.sa_handler = removeAndEnd;
    sigemptyset(&ours.sa_mask);
    for (const int other : kSignals) {
      sigaddset(&ours.sa_mask, other);
    }
    if (sigaction(kSignals[i], &ours, &previous[i]) != 0) {
      const int error = errno;
      restoreLocked();
      return std::unexpected(std::format("sigaction({}) could not be set: {}", kSignals[i], std::strerror(error)));
    }
    installed[i] = true;
    handled.push_back(kSignals[i]);
  }
  return handled;
}

void disarmFileRemovalOnSignal() {
  std::lock_guard lock(armMutex);
  restoreLocked();
}

}
