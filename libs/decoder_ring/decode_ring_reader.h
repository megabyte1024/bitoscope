/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>

#include "bulk_ring.h"

namespace bitoscope::plugin_ring {

enum class DecodeRecordKind {
  Planes,
  Skip,
};

struct DecodeRecord {
  DecodeRecordKind kind = DecodeRecordKind::Planes;
  uint64_t first_sample = 0;
  uint64_t num_samples = 0;
  uint32_t num_channels = 0;
  uint64_t plane_stride = 0;
  std::span<const uint8_t> payload;
  uint64_t slot = 0;

  std::span<const uint8_t> plane(uint32_t channel) const;
  bool level(uint32_t channel) const;
};

enum class DecodeReadStatus {
  Record,
  Pending,
  Ended,
  Failed,
  Invalid,
  WindowFull,
};

struct DecodeRead {
  DecodeReadStatus status = DecodeReadStatus::Pending;
  DecodeRecord record;
  bitoscope::bulk_ring::FailedRecord failed;
  std::string error;
};

class DecodeRingReader {
 public:
  static std::expected<DecodeRingReader, std::string> open(std::string_view name, uint64_t job_id,
                                                           uint32_t num_channels,
                                                           bitoscope::bulk_ring::Timing timing = {});

  DecodeRingReader(DecodeRingReader&& other) noexcept;
  DecodeRingReader& operator=(DecodeRingReader&& other) noexcept;
  DecodeRingReader(const DecodeRingReader&) = delete;
  DecodeRingReader& operator=(const DecodeRingReader&) = delete;
  ~DecodeRingReader();

  const std::string& name() const { return ring_.name(); }
  uint64_t jobId() const { return ring_.captureId(); }
  uint32_t numChannels() const { return num_channels_; }
  uint64_t nextSample() const { return next_sample_; }
  const bitoscope::bulk_ring::BulkRing& ring() const { return ring_; }

  DecodeRead next(uint64_t deadline_ms);

  std::expected<void, std::string> release();

  DecodeRead take(uint64_t deadline_ms);

  std::expected<void, std::string> releaseSlotsBefore(uint64_t slot);

  uint32_t slotCount() const { return ring_.slotCount(); }
  void heartbeat() { ring_.heartbeat(); }

  void close();

 private:
  DecodeRingReader(bitoscope::bulk_ring::BulkRing ring, uint32_t num_channels);
  DecodeRead fetch(uint64_t deadline_ms);

  bitoscope::bulk_ring::BulkRing ring_;
  bool live_ = true;
  uint32_t num_channels_ = 0;
  uint64_t next_sample_ = 0;
  bool held_ = false;
  DecodeRecord held_record_;
  std::string invalid_;
};

}
