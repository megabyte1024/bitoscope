/*
 * SPDX-License-Identifier: MIT
 */

#include "history_ring_floor.h"

#include <algorithm>
#include <limits>

#include "capture_allocator.h"

namespace bitoscope::capture {

uint64_t historyRingWindows(const Reservation& reservation) {
  const CapturePlan& plan = reservation.plan;
  const uint64_t c = plan.channels();
  const uint64_t s = (uint64_t{plan.samplesPerWindow()} + 7) / 8;
  if (c == 0 || s == 0) return 0;
  const uint64_t arena = reservation.arenaBytes();
  const uint64_t n = reservation.blockCount();
  constexpr uint64_t kMax = (std::numeric_limits<uint64_t>::max)();
  const uint64_t tail = s - 1;
  const uint64_t margin = tail != 0 && n > kMax / tail ? kMax : n * tail;
  if (arena < margin) return 0;
  return (arena - margin) / (s * c);
}

uint64_t historyRingSamples(const Reservation& reservation) {
  const CapturePlan& plan = reservation.plan;
  const uint64_t windows = historyRingWindows(reservation);
  if (windows >= plan.slots()) return plan.totalSamples();
  return windows * plan.samplesPerWindow();
}

uint64_t triggeredArenaCap(const CapturePlan& plan, uint64_t block_bytes) {
  constexpr uint64_t kMax = (std::numeric_limits<uint64_t>::max)();
  const auto add = [](uint64_t a, uint64_t b) { return a > kMax - b ? kMax : a + b; };
  const auto mul = [](uint64_t a, uint64_t b) { return b != 0 && a > kMax / b ? kMax : a * b; };
  const uint64_t c = plan.channels();
  const uint64_t s = (uint64_t{plan.samplesPerWindow()} + 7) / 8;
  if (c == 0 || s == 0) return plan.totalBytes();
  const uint64_t need = mul(mul(plan.slots(), c), s);
  const uint64_t tail = s - 1;

  const uint64_t single = add(need, tail);
  if (single <= block_bytes) return single;

  const uint64_t b = (std::max)(block_bytes, s);
  const uint64_t per_block = b - tail;
  const uint64_t n = need / per_block + (need % per_block != 0 ? 1 : 0);
  return add(need, mul(n, tail));
}

}
