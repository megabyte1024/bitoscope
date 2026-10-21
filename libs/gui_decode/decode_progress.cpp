/*
 * SPDX-License-Identifier: MIT
 */

#include "decode_progress.h"

#include <algorithm>

namespace bitoscope::gui_decode {

ProgressClock steadyProgressClock() {
  return [] {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch());
  };
}

SnapshotPacer::SnapshotPacer(std::chrono::nanoseconds start, const decoder_host::PacingPolicy& policy)
    : policy_(policy), started_(start), next_(start + std::chrono::milliseconds(policy.first_picture_ms)) {}

bool SnapshotPacer::due(std::chrono::nanoseconds now) const { return now >= next_; }

void SnapshotPacer::taken(std::chrono::nanoseconds start, std::chrono::nanoseconds end) {
  const std::chrono::nanoseconds first = std::chrono::milliseconds(policy_.first_picture_ms);
  const std::chrono::nanoseconds longest = std::chrono::milliseconds(policy_.longest_wait_ms);
  const std::chrono::nanoseconds grown = (start - started_) * policy_.growth_percent / 100;
  const std::chrono::nanoseconds wait = (std::min)(longest, (std::max)(first, grown));
  next_ = start + (std::max)(wait, (end - start) * kProgressCostFactor);
}

}
