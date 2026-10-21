/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include "bulk_ring.h"
#include "chunk_print_policy.h"
#include "chunk_summary.h"

namespace bitoscope::cli {

struct RingSummaryConfig {
  uint64_t wait_slice_ms = 10;
  uint64_t chunk_interval = bitoscope::host::kChunkPrintInterval;
  std::chrono::steady_clock::duration time_interval = bitoscope::host::kChunkPrintTimeInterval;
  std::function<std::chrono::steady_clock::time_point()> now;
};

enum class RingSummaryEnd {
  Running,
  Ended,
  Failed,
  Invalid,
  Stopped,
};

struct RingSummaryTotals {
  uint64_t chunks = 0;
  uint64_t samples = 0;
  uint64_t missing_samples = 0;
  uint64_t next_sample = 0;
  RingSummaryEnd end = RingSummaryEnd::Running;
};

class RingSummary {
 public:
  using LineFn = std::function<void(const std::string& line)>;

  static std::expected<std::unique_ptr<RingSummary>, std::string> start(bitoscope::bulk_ring::BulkRing& ring,
                                                                        std::string plugin_id, LineFn emit,
                                                                        RingSummaryConfig config = {});

  ~RingSummary();

  RingSummary(const RingSummary&) = delete;
  RingSummary& operator=(const RingSummary&) = delete;

  RingSummaryTotals finish(std::chrono::milliseconds limit);

  void stop();

  RingSummaryTotals totals() const;

 private:
  RingSummary(bitoscope::bulk_ring::BulkRing& ring, std::string plugin_id, LineFn emit,
              RingSummaryConfig config);

  void run();
  void count(const bitoscope::bulk_ring::ChunkHeader& header);
  std::string countLineLocked();
  std::chrono::steady_clock::time_point now() const;

  bitoscope::bulk_ring::BulkRing& ring_;
  const std::string plugin_id_;
  const LineFn emit_;
  const RingSummaryConfig config_;

  mutable std::mutex mutex_;
  RingSummaryTotals totals_;
  bitoscope::host::ChunkGapTracker gaps_;
  uint64_t chunks_since_line_ = 0;
  uint64_t samples_since_line_ = 0;
  uint64_t missing_since_line_ = 0;
  uint64_t last_rate_milli_hz_ = 0;
  uint64_t last_first_sample_ = 0;
  std::optional<std::chrono::steady_clock::time_point> last_line_;
  bool finished_ = false;

  std::atomic<bool> stop_{false};
  std::atomic<RingSummaryEnd> end_{RingSummaryEnd::Running};
  std::condition_variable ended_cv_;
  std::thread thread_;  // WHY: Declared last: thread starts only once every member it uses exists.
};

}
