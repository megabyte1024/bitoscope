/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>

#include "pacing_policy.h"

namespace bitoscope::gui_decode {

class DecodedAnnotations;

struct DecodeProgress {
  std::uint64_t watermark = 0;
  std::uint64_t captureSamples = 0;
  std::shared_ptr<const DecodedAnnotations> snapshot;
};

// WHY: Waiting 20 times a copy's cost keeps copies under 5% of decoder time at any prefix size.
inline constexpr int kProgressCostFactor = 20;

using ProgressClock = std::function<std::chrono::nanoseconds()>;
ProgressClock steadyProgressClock();

class SnapshotPacer {
 public:
  explicit SnapshotPacer(std::chrono::nanoseconds start = {},
                         const decoder_host::PacingPolicy& policy = decoder_host::defaultPacingPolicy());
  bool due(std::chrono::nanoseconds now) const;
  void taken(std::chrono::nanoseconds start, std::chrono::nanoseconds end);

 private:
  decoder_host::PacingPolicy policy_;
  std::chrono::nanoseconds started_;
  std::chrono::nanoseconds next_;
};

}
