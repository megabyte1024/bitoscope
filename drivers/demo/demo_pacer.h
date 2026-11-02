/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <chrono>
#include <cstdint>

namespace demo_driver {

using PaceClock = std::chrono::steady_clock;

inline constexpr std::chrono::milliseconds kPaceBurst{2};
inline constexpr unsigned kPaceCatchUp = 2;

std::chrono::nanoseconds durationOf(uint64_t samples, uint64_t rate_hz);

class ChunkPacer {
 public:
  ChunkPacer(PaceClock::time_point start, uint64_t rate_hz,
             std::chrono::nanoseconds burst = kPaceBurst, unsigned catch_up = kPaceCatchUp);

  PaceClock::time_point next(uint64_t end_sample, uint64_t samples, PaceClock::time_point now);

  PaceClock::time_point dueOf(uint64_t end_sample) const;

 private:
  PaceClock::time_point start_;
  uint64_t rate_hz_;
  std::chrono::nanoseconds burst_;
  unsigned catch_up_;
  PaceClock::time_point last_;
};

class PreciseWait {
 public:
  PreciseWait();
  ~PreciseWait();
  PreciseWait(const PreciseWait&) = delete;
  PreciseWait& operator=(const PreciseWait&) = delete;

  void until(PaceClock::time_point until);

  bool highResolution() const { return timer_ != nullptr; }

 private:
  void* timer_ = nullptr;
};

}
