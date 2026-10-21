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

namespace bitoscope::host {

enum class DecodeRingErrorKind {
  BadInput,
  Timeout,
  ConsumerClosed,
  ConsumerStale,
  ConsumerInvalid,
  Finished,
};

struct DecodeRingError {
  DecodeRingErrorKind kind = DecodeRingErrorKind::BadInput;
  std::string message;
};

struct DecodeRingSpec {
  uint32_t num_channels = 0;
  uint64_t sample_rate_milli_hz = 0;
  uint32_t slot_count = 0;
  uint64_t slot_payload_bytes = 0;
  uint32_t flush_after_ms = bitoscope::bulk_ring::kDefaultFlushAfterMs;
  bool fill_slots = false;
};

class DecodeRingProducer {
 public:
  static std::expected<DecodeRingProducer, std::string> create(std::string_view name, uint64_t job_id,
                                                               const DecodeRingSpec& spec,
                                                               bitoscope::bulk_ring::Timing timing = {});

  DecodeRingProducer(DecodeRingProducer&& other) noexcept;
  DecodeRingProducer& operator=(DecodeRingProducer&& other) noexcept;
  DecodeRingProducer(const DecodeRingProducer&) = delete;
  DecodeRingProducer& operator=(const DecodeRingProducer&) = delete;
  ~DecodeRingProducer();

  const std::string& name() const { return ring_.name(); }
  uint64_t jobId() const { return ring_.captureId(); }
  uint32_t numChannels() const { return num_channels_; }
  uint64_t nextSample() const { return next_sample_; }
  uint64_t maxChunkSamples() const { return max_chunk_samples_; }
  const bitoscope::bulk_ring::BulkRing& ring() const { return ring_; }

  std::expected<void, DecodeRingError> writePlanes(std::span<const std::span<const uint8_t>> planes,
                                                   uint64_t first_bit, uint64_t num_samples,
                                                   uint64_t deadline_ms);

  std::expected<void, DecodeRingError> writeSkip(std::span<const uint8_t> levels, uint64_t num_samples,
                                                 uint64_t deadline_ms);

  void flush();

  std::expected<void, DecodeRingError> end();

  std::expected<void, DecodeRingError> fail(std::string_view reason);

 private:
  DecodeRingProducer(bitoscope::bulk_ring::BulkRing ring, uint32_t num_channels, uint64_t sample_rate_milli_hz);

  std::expected<std::span<uint8_t>, DecodeRingError> room(uint64_t bytes, uint64_t pending, uint64_t deadline_ms);
  std::expected<void, DecodeRingError> commitRecord(uint32_t encoding, uint64_t num_samples, uint64_t bytes);

  struct Gathering {
    std::span<uint8_t> room;
    uint64_t samples = 0;
    bool held = false;
  };
  std::expected<void, DecodeRingError> writePlanesWhole(std::span<const std::span<const uint8_t>> planes,
                                                        uint64_t first_bit, uint64_t num_samples,
                                                        uint64_t deadline_ms);
  std::expected<void, DecodeRingError> writeSkipWhole(std::span<const uint8_t> levels, uint64_t num_samples,
                                                      uint64_t deadline_ms);
  std::expected<void, DecodeRingError> gather(uint64_t pending, uint64_t deadline_ms);
  std::expected<void, DecodeRingError> gatherLevels(std::span<const uint8_t> levels, uint64_t num_samples);
  std::expected<void, DecodeRingError> commitGathered();

  bitoscope::bulk_ring::BulkRing ring_;
  bool live_ = true;
  uint32_t num_channels_ = 0;
  uint64_t sample_rate_milli_hz_ = 0;
  uint64_t max_chunk_samples_ = 0;
  uint64_t next_sample_ = 0;
  bool fill_slots_ = false;
  Gathering gathering_;
};

}
