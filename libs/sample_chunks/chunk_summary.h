/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

namespace bitoscope::host {

class ChunkGapTracker {
 public:
  uint64_t observe(uint64_t first_sample_index, uint64_t num_samples);

  void expectFrom(uint64_t first_sample_index);

  uint64_t totalMissing() const { return total_missing_; }

 private:
  bool have_expected_ = false;
  uint64_t expected_next_index_ = 0;
  uint64_t total_missing_ = 0;
};

struct ChunkSummary {
  uint64_t chunks = 0;
  uint64_t samples = 0;
  uint64_t sample_rate_milli_hz = 0;
  uint64_t last_first_sample_index = 0;
  uint64_t missing_samples = 0;
  uint64_t total_missing_samples = 0;
};

std::string formatChunkSummary(std::string_view driver_device_id, const ChunkSummary& summary);

inline void ChunkGapTracker::expectFrom(uint64_t first_sample_index) {
  have_expected_ = true;
  expected_next_index_ = first_sample_index;
}

inline uint64_t ChunkGapTracker::observe(uint64_t first_sample_index, uint64_t num_samples) {
  constexpr uint64_t kMax = std::numeric_limits<uint64_t>::max();

  uint64_t missing = 0;
  if (have_expected_ && first_sample_index > expected_next_index_) {
    missing = first_sample_index - expected_next_index_;
  }
  total_missing_ = missing > kMax - total_missing_ ? kMax : total_missing_ + missing;

  // WHY: Saturating, so a chunk claiming to run past the top of the range cannot wrap and fake a huge gap.
  expected_next_index_ = num_samples > kMax - first_sample_index ? kMax : first_sample_index + num_samples;
  have_expected_ = true;
  return missing;
}

inline std::string formatChunkSummary(std::string_view driver_device_id, const ChunkSummary& summary) {
  const std::string rate = summary.sample_rate_milli_hz % 1000 == 0
                               ? std::to_string(summary.sample_rate_milli_hz / 1000) + " Hz"
                               : std::to_string(summary.sample_rate_milli_hz) + " mHz";
  std::string line = "[" + std::string(driver_device_id) + "] la.acquisition: " +
                     std::to_string(summary.chunks) + " chunk(s), " +
                     std::to_string(summary.samples) + " samples @ " + rate + " (last index " +
                     std::to_string(summary.last_first_sample_index) + ")";
  if (summary.missing_samples > 0) {
    line += ", " + std::to_string(summary.missing_samples) + " samples MISSING (" +
            std::to_string(summary.total_missing_samples) + " in total)";
  }
  return line;
}

}
