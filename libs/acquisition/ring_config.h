/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <expected>
#include <optional>
#include <string>

namespace bitoscope::acquisition {

inline constexpr uint64_t kRingSlotMiB = 4;
inline constexpr uint64_t kDefaultRingMiB = 256;
inline constexpr uint32_t kMinRingSlots = 2;
inline constexpr uint32_t kMaxPackWorkers = 1024;
inline constexpr uint32_t kMinPackWorkers = 2;
inline constexpr uint64_t kPackWorkerBytesPerSecond = 1'000'000'000;

struct RingGeometry {
  uint32_t slot_count = 0;
  uint64_t slot_payload_bytes = 0;

  bool operator==(const RingGeometry&) const = default;
};

std::expected<RingGeometry, std::string> ringGeometryForMiB(uint64_t mib);

std::expected<uint64_t, std::string> ringMappingBytes(const RingGeometry& geometry);

// WHY: Sum saturates instead of wrapping: too large a figure refuses the capture rather than holding back too little.
std::expected<uint64_t, std::string> acquisitionHeldBackBytes(uint64_t capture_held_back,
                                                              const std::optional<RingGeometry>& ring);

uint64_t captureBytesPerSecond(uint32_t channels, uint64_t sample_rate_milli_hz);

uint32_t defaultPackWorkers(uint32_t hardware_threads, uint64_t bytes_per_second);
uint32_t defaultClearingWorkers(uint32_t hardware_threads);

}
