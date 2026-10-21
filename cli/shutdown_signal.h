/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <atomic>
#include <csignal>

namespace bitoscope::cli {

class ShutdownSignal {
 public:
  static void install() {
    if (installed_.exchange(true)) {
      return;
    }
    previous_int_ = std::signal(SIGINT, &ShutdownSignal::handler);
    previous_term_ = std::signal(SIGTERM, &ShutdownSignal::handler);
#ifdef SIGBREAK
    previous_break_ = std::signal(SIGBREAK, &ShutdownSignal::handler);
#endif
  }

  static void uninstall() {
    if (!installed_.exchange(false)) {
      return;
    }
    std::signal(SIGINT, previous_int_);
    std::signal(SIGTERM, previous_term_);
#ifdef SIGBREAK
    std::signal(SIGBREAK, previous_break_);
#endif
  }

  static bool requested() { return requested_.load(std::memory_order_acquire); }

  static void request() { requested_.store(true, std::memory_order_release); }

  // WHY: Hook runs inside signal handler before repeat signal ends process: must be async-signal-safe.
  static void onGiveUp(void (*hook)()) { give_up_hook_.store(hook, std::memory_order_release); }

  static void reset() {
    requested_.store(false, std::memory_order_release);
    signals_.store(0, std::memory_order_release);
  }

  static bool onSignal() {
    if (signals_.fetch_add(1, std::memory_order_acq_rel) > 0) {
      if (void (*hook)() = give_up_hook_.load(std::memory_order_acquire)) hook();
      return true;
    }
    requested_.store(true, std::memory_order_release);
    return false;
  }

 private:
  static void handler(int signal_number) {
    if (onSignal()) {
      std::signal(signal_number, SIG_DFL);
      std::raise(signal_number);
      return;
    }
    // WHY: Some C runtimes reset disposition to default when handler runs: re-arm so second signal is recognized.
    std::signal(signal_number, &ShutdownSignal::handler);
  }

  static_assert(std::atomic<bool>::is_always_lock_free && std::atomic<int>::is_always_lock_free &&
                    std::atomic<void (*)()>::is_always_lock_free,
                "signal handler may only touch lock-free atomics");

  static inline std::atomic<bool> requested_{false};
  static inline std::atomic<int> signals_{0};
  static inline std::atomic<bool> installed_{false};
  static inline std::atomic<void (*)()> give_up_hook_{nullptr};
  static inline void (*previous_int_)(int) = SIG_DFL;
  static inline void (*previous_term_)(int) = SIG_DFL;
#ifdef SIGBREAK
  static inline void (*previous_break_)(int) = SIG_DFL;
#endif
};

}
