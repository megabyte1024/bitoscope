/*
 * SPDX-License-Identifier: MIT
 */

#include "demo_pacer.h"

#include <algorithm>
#include <thread>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace demo_plugin {

std::chrono::nanoseconds durationOf(uint64_t samples, uint64_t rate_hz) {
  const uint64_t whole = samples / rate_hz;
  const uint64_t rest = samples % rate_hz;  // WHY: rest < rate_hz <= 5e8, so rest * 1e9 stays below 2^63.
  return std::chrono::nanoseconds(static_cast<int64_t>(whole * 1'000'000'000ull + rest * 1'000'000'000ull / rate_hz));
}

ChunkPacer::ChunkPacer(PaceClock::time_point start, uint64_t rate_hz, std::chrono::nanoseconds burst,
                       unsigned catch_up)
    : start_(start), rate_hz_(rate_hz), burst_(burst), catch_up_((std::max)(catch_up, 1u)), last_(start) {}

PaceClock::time_point ChunkPacer::dueOf(uint64_t end_sample) const {
  return start_ + durationOf(end_sample, rate_hz_);
}

PaceClock::time_point ChunkPacer::next(uint64_t end_sample, uint64_t samples, PaceClock::time_point now) {
  const auto spacing = durationOf(samples, rate_hz_) / catch_up_;
  const PaceClock::time_point at = (std::max)({dueOf(end_sample), last_ + spacing, now - burst_});
  last_ = at;
  return at;
}

#if defined(_WIN32)

PreciseWait::PreciseWait() {
  timer_ = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
}

PreciseWait::~PreciseWait() {
  if (timer_ != nullptr) {
    CloseHandle(static_cast<HANDLE>(timer_));
  }
}

void PreciseWait::until(PaceClock::time_point until) {
  if (timer_ == nullptr) {
    std::this_thread::sleep_until(until);
    return;
  }
  // WHY: The timer is set RELATIVE (negative due time, 100 ns units); the loop waits again because the two clocks
  // round differently.
  for (auto left = until - PaceClock::now(); left > PaceClock::duration::zero(); left = until - PaceClock::now()) {
    const auto hundreds = std::chrono::duration_cast<std::chrono::duration<int64_t, std::ratio<1, 10'000'000>>>(left);
    LARGE_INTEGER due{};
    due.QuadPart = -(std::max)(hundreds.count(), int64_t{1});
    if (!SetWaitableTimer(static_cast<HANDLE>(timer_), &due, 0, nullptr, nullptr, FALSE) ||
        WaitForSingleObject(static_cast<HANDLE>(timer_), INFINITE) != WAIT_OBJECT_0) {
      std::this_thread::sleep_until(until);
      return;
    }
  }
}

#else

PreciseWait::PreciseWait() = default;

PreciseWait::~PreciseWait() = default;

void PreciseWait::until(PaceClock::time_point until) { std::this_thread::sleep_until(until); }

#endif

}
