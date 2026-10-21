/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <atomic>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <thread>

#include "bulk_ring.h"
#include "capture_session.h"
#include "host_ring.h"
#include "ring_config.h"

namespace bitoscope::acquisition {

struct RingPumpConfig {
  uint64_t wait_slice_ms = 10;
  std::function<void(std::span<const uint8_t> payload)> before_release_for_test;
};

enum class RingPumpEnd {
  Running,
  SessionOver,
  Ended,
  Failed,
  Invalid,
  Stopped,
};

class RingPump {
 public:
  static std::expected<std::unique_ptr<RingPump>, std::string> start(
      bitoscope::bulk_ring::BulkRing& ring,
      std::shared_ptr<bitoscope::capture::CaptureSession> session, RingPumpConfig config = {});

  ~RingPump();

  RingPump(const RingPump&) = delete;
  RingPump& operator=(const RingPump&) = delete;

  void stop();

  uint64_t peakSlotsInUse() const { return peak_.load(std::memory_order_acquire); }
  uint64_t chunksRead() const { return chunks_read_.load(std::memory_order_acquire); }
  RingPumpEnd end() const { return end_.load(std::memory_order_acquire); }

 private:
  RingPump(bitoscope::bulk_ring::BulkRing& ring,
           std::shared_ptr<bitoscope::capture::CaptureSession> session, RingPumpConfig config);

  void run();
  void deliver(const bitoscope::bulk_ring::ReadChunk& chunk);

  bitoscope::bulk_ring::BulkRing& ring_;
  std::shared_ptr<bitoscope::capture::CaptureSession> session_;
  RingPumpConfig config_;
  std::atomic<bool> stop_{false};
  std::atomic<uint64_t> peak_{0};
  std::atomic<uint64_t> chunks_read_{0};
  std::atomic<RingPumpEnd> end_{RingPumpEnd::Running};
  std::thread thread_;  // WHY: Declared last: the thread starts once everything it uses exists.
};

// WHY: end() and the destructor stop the pump BEFORE destroying the
// ring: a pump must not outlive the mapping it reads.
class AttemptRing {
 public:
  static std::expected<std::unique_ptr<AttemptRing>, std::string> start(
      const CreateRingFn& create, const RingGeometry& geometry, uint64_t capture_id,
      std::shared_ptr<bitoscope::capture::CaptureSession> session, RingPumpConfig config = {});

  ~AttemptRing() { end(); }
  AttemptRing(const AttemptRing&) = delete;
  AttemptRing& operator=(const AttemptRing&) = delete;

  const std::string& name() const { return name_; }
  uint32_t slotCount() const { return slot_count_; }

  void end();

  uint64_t peakSlotsInUse() const { return pump_ ? pump_->peakSlotsInUse() : peak_; }
  uint64_t slotsInUse() const;
  uint64_t chunksRead() const { return pump_ ? pump_->chunksRead() : chunks_read_; }

 private:
  AttemptRing() = default;

  std::string name_;
  uint32_t slot_count_ = 0;
  std::unique_ptr<bitoscope::bulk_ring::BulkRing> ring_;
  std::unique_ptr<RingPump> pump_;
  uint64_t peak_ = 0;
  uint64_t chunks_read_ = 0;
};

}
