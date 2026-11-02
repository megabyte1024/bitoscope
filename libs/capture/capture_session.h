/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <atomic>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

#include "block_supply_ahead.h"
#include "capture_extent.h"
#include "capture_plan.h"
#include "capture_snapshot.h"
#include "chunk_view.h"
#include "evaluator.h"
#include "pack_pool.h"
#include "raw_capture_buffer.h"
#include "trigger_spec.h"
#include "window_assembler.h"

namespace bitoscope::capture {

class GrowingArenaMemory;

inline constexpr uint64_t kTailRefreshMs = 50;

enum class CaptureStatus { Running, Complete, Full, Interrupted, TimedOut, Failed, Stopped };

enum class FailureKind {
  StartUpLoss,
  Gap,
  Dropped,
  Rejected,
  DriverIgnoresId,
  TriggerShortfall,
  Stalled,
  EndedEarly,
  StoreError
};

struct CaptureFailure {
  FailureKind kind = FailureKind::Rejected;
  uint64_t sample_index = 0;
  uint64_t lost_samples = 0;
  std::string message;
};

struct SessionConfig {
  uint64_t inactivity_timeout_ms = 5000;
  std::function<uint64_t()> now_ms;
  uint64_t capture_id = 0;
  std::string driver_device_id;
  std::optional<bitoscope::trigger::TriggerSpec> trigger;
  uint32_t pack_workers = 0;
  PackPoolHooks pack_hooks_for_test;
  uint32_t window_pool_buffers_for_test = 0;
  uint64_t buffer_wait_poll_ms = 10;
  uint32_t pack_depth_for_test = 0;
  bool live_tail = false;
  std::shared_ptr<BlockSupplyAhead> block_supply;
  std::shared_ptr<const GrowingArenaMemory> arena_memory;
};

struct CaptureSummary {
  CaptureStatus end = CaptureStatus::Running;
  uint64_t requested_samples = 0;
  uint64_t stored_samples = 0;
  uint64_t not_stored_samples = 0;
  uint64_t shortfall_samples = 0;
  uint64_t capture_id = 0;
  uint64_t foreign_chunks = 0;
};

struct CaptureProgress {
  uint64_t requested_samples = 0;
  uint64_t stored_samples = 0;
  uint64_t memory_used_bytes = 0;
  uint64_t memory_bytes = 0;
  bool armed = false;
  uint64_t armed_ms = 0;
  bool history_complete = false;
};

class CaptureSession {
 public:
  static std::expected<std::unique_ptr<CaptureSession>, std::string> create(RawCaptureBuffer&& buffer,
      SessionConfig config);

  ~CaptureSession();

  CaptureSession(const CaptureSession&) = delete;
  CaptureSession& operator=(const CaptureSession&) = delete;

  std::expected<void, std::string> submit(const bitoscope::host::ChunkView& chunk);

  void deviceStarted() {
    std::lock_guard lock(mutex_);
    last_activity_ms_ = config_.now_ms();
    armed_at_ms_ = last_activity_ms_;
  }

  uint64_t chunksReceived() const {
    std::lock_guard lock(mutex_);
    return chunks_received_;
  }

  uint64_t foreignChunks() const {
    std::lock_guard lock(mutex_);
    return foreign_chunks_;
  }

  bool timedOutBeforeFirstChunk() const {
    std::lock_guard lock(mutex_);
    return observeLocked() == CaptureStatus::TimedOut && timed_out_before_first_chunk_;
  }

  // WHY: requestStop takes NO lock: submit() holds the session mutex while it waits for packers, so a locking stop
  // would wait for that wait.
  void requestStop() {
    stop_requested_.store(true, std::memory_order_release);
    abandonWaits();
  }

  void requestKeepStop() { keep_stop_requested_.store(true, std::memory_order_release); }

  void requestSessionStop() { session_stop_requested_.store(true, std::memory_order_release); }

  void failSource(uint64_t at_sample, uint64_t lost_samples, std::string_view reason);

  void sourceEnded();

  void sourceInvalid(std::string_view reason);

  // WHY: The first non-Running reading is latched: a late chunk could turn a capture already ruled over into
  // Complete, and it would be saved.
  CaptureStatus status() const {
    std::lock_guard lock(mutex_);
    return observeLocked();
  }

  std::expected<CaptureSummary, std::string> finish();

  std::expected<uint64_t, std::string> releaseUnusedMemory() {
    std::lock_guard lock(mutex_);
    if (!finished_) return std::unexpected("capture session is not finished");
    return buffer_.releaseUnusedBlocks();
  }

  std::expected<Reservation, std::string> releaseReservation() {
    std::lock_guard lock(mutex_);
    if (!finished_) return std::unexpected("capture session is not finished");
    return buffer_.releaseReservation();
  }

  std::optional<CaptureFailure> failure() const {
    std::lock_guard lock(mutex_);
    adoptCommitFailureLocked();
    return failure_;
  }

  std::optional<uint64_t> triggerOrigin() const {
    std::lock_guard lock(mutex_);
    return trigger_origin_;
  }

  std::optional<uint64_t> triggerSampleIndex() const {
    std::lock_guard lock(mutex_);
    return trigger_sample_index_;
  }

  CaptureExtent captureExtent() const {
    std::lock_guard lock(mutex_);
    auto commits = commitLock();
    return captureExtentLocked();
  }

  CaptureSnapshot snapshot() const { return published_.read(); }

  CaptureSnapshot snapshot(CaptureTail& tail) const { return published_.read(tail); }

  bool publishesTail() const { return tail_enabled_; }

  bool triggerTimedOut() const {
    std::lock_guard lock(mutex_);
    return latched_ ? latched_timeout_ : triggerTimedOutLocked();
  }

  CaptureProgress progress() const;

  uint64_t memoryStillNeeded(uint64_t free_now) const;

  const RawCaptureBuffer& buffer() const { return buffer_; }

  uint32_t windowPoolCapacityForTest() const { return assembler_.poolCapacityForTest(); }

  uint32_t packSlotsForTest() const { return pack_pool_ ? pack_pool_->depth() : 0; }
  uint32_t packWorkersForTest() const { return pack_pool_ ? pack_pool_->workers() : 0; }
  uint64_t packScratchBytesForTest() const { return pack_pool_ ? pack_pool_->scratchBytes() : 0; }
  uint64_t windowBufferBytesForTest() const { return assembler_.poolStorageBytesForTest(); }

 private:
  CaptureSession(RawCaptureBuffer&& buffer, bitoscope::host::WindowAssembler&& assembler,
      SessionConfig config, std::optional<bitoscope::trigger::Evaluator> evaluator);

  bool triggered() const { return evaluator_.has_value(); }

  void clipAndAppend(bitoscope::host::WindowLease& lease, uint64_t end);

  void store(bitoscope::host::WindowLease& lease);

  std::expected<AppendOutcome, std::string> appendInline(bitoscope::host::WindowLease& lease,
      uint64_t seq);

  void commitPacked(const bitoscope::host::WindowInfo& info,
      const std::expected<PackedWindow, std::string>& packed);

  void recordOutcome(const bitoscope::host::WindowInfo& info,
      const std::expected<AppendOutcome, std::string>& stored);

  void recordCommitFailure(FailureKind kind, const bitoscope::host::WindowInfo& info,
      std::string_view why);

  std::string commitFailureText(FailureKind kind, uint64_t sample, uint64_t samples,
      std::string_view why) const;

  void adoptCommitFailureLocked() const;

  std::optional<std::string> takeCommitErrorLocked();

  std::string commitErrorText() const {
    if (!commit_error_.empty()) return commit_error_;
    if (failure_ && failure_->kind == commit_failure_kind_) return failure_->message;
    return "window could not be stored, so nothing from it on is";
  }

  // WHY: Lock order: session mutex first, then commit mutex. Inline, commits run under the session mutex and this
  // lock is empty.
  std::unique_lock<std::mutex> commitLock() const {
    return pack_pool_ ? pack_pool_->lockCommits() : std::unique_lock<std::mutex>();
  }

  bool allCommitted() const { return !pack_pool_ || pack_pool_->idle(); }

  void onTriggeredWindow(bitoscope::host::WindowLease lease);

  void settleOnHit(uint64_t trigger);

  void joinHistoryPass() {
    if (history_pass_.joinable()) history_pass_.join();
  }

  static bool windowOutlastsTailRefresh(const CapturePlan& plan) {
    return uint64_t{plan.samplesPerWindow()} * 1'000'000 > kTailRefreshMs * plan.sampleRateMilliHz();
  }

  void refreshTailLocked();

  void publishSnapshotLocked() {
    // WHY: Call under commitLock() (inline: the session mutex): PublishedCaptureSnapshot takes one writer at a time.
    published_.publish(captureExtentLocked(), trigger_origin_, trigger_sample_index_);
  }

  CaptureExtent captureExtentLocked() const;

  void failInWindow(FailureKind kind, uint64_t sample_index, uint64_t lost, std::string message);

  void abandonWaits();

  std::unexpected<std::string> recordStall(uint64_t sample, const std::string& what);

  std::unexpected<std::string> fail(FailureKind kind, uint64_t sample_index, uint64_t lost,
      std::string message);

  std::unexpected<std::string> reject(const bitoscope::host::ChunkView& chunk,
      const std::string& why);

  CaptureStatus statusLocked() const;

  CaptureStatus observeLocked() const;

  bool triggerTimedOutLocked() const;

  mutable std::mutex mutex_;
  RawCaptureBuffer buffer_;
  bitoscope::host::WindowAssembler assembler_;
  SessionConfig config_;
  bitoscope::host::WindowAssembler::WindowFn on_window_;
  std::optional<bitoscope::trigger::Evaluator> evaluator_;
  uint64_t last_activity_ms_ = 0;
  uint64_t armed_at_ms_ = 0;
  uint64_t covered_ = 0;
  uint64_t next_expected_ = 0;
  bool seen_chunk_ = false;
  uint64_t foreign_chunks_ = 0;
  uint64_t chunks_received_ = 0;
  mutable std::optional<CaptureFailure> failure_;
  bool trigger_fired_ = false;
  std::optional<uint64_t> store_origin_;
  uint64_t front_offset_ = 0;
  uint64_t store_end_ = 0;
  std::optional<uint64_t> absolute_end_;
  std::optional<uint64_t> trigger_origin_;
  std::optional<uint64_t> trigger_sample_index_;
  bool reached_ = false;
  std::atomic<bool> stop_requested_{false};
  std::atomic<bool> keep_stop_requested_{false};
  std::atomic<bool> session_stop_requested_{false};
  std::thread history_pass_;
  std::atomic<bool> history_stop_{false};
  std::atomic<bool> abandon_waits_{false};
  std::atomic<bool> arena_closed_{false};
  bitoscope::host::WaitLimit wait_limit_;
  bool finished_ = false;
  std::expected<CaptureSummary, std::string> finish_result_;
  mutable std::optional<CaptureStatus> latched_;
  mutable bool latched_timeout_ = false;
  mutable bool timed_out_before_first_chunk_ = false;
  CaptureStatus end_ = CaptureStatus::Running;
  bool commit_failed_ = false;
  FailureKind commit_failure_kind_ = FailureKind::StoreError;
  uint64_t commit_origin_ = 0;
  std::string commit_error_;
  bool commit_error_reported_ = false;
  mutable std::optional<CaptureFailure> commit_failure_;
  uint64_t skipped_samples_ = 0;
  std::string window_failure_;
  uint64_t inline_seq_ = 0;
  std::optional<PackScratch> inline_test_scratch_;
  PublishedCaptureSnapshot published_;
  bool tail_enabled_ = false;
  std::optional<uint64_t> tail_refreshed_ms_;
  // WHY: Declared last and reset first by the destructor: its workers commit into the members above.
  std::unique_ptr<PackPool> pack_pool_;
};

}
