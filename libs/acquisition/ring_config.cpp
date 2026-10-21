/*
 * SPDX-License-Identifier: MIT
 */

#include "ring_config.h"

#include <algorithm>
#include <limits>

#include "bulk_ring.h"

namespace bitoscope::acquisition {

namespace {

constexpr uint64_t kMiB = uint64_t{1024} * 1024;

}

std::expected<RingGeometry, std::string> ringGeometryForMiB(uint64_t mib) {
  if (mib % kRingSlotMiB != 0) {
    return std::unexpected("ring is made of " + std::to_string(kRingSlotMiB) +
                           " MiB slots, so its size must be multiple of " +
                           std::to_string(kRingSlotMiB) + " MiB; " + std::to_string(mib) +
                           " is not");
  }
  const uint64_t slots = mib / kRingSlotMiB;
  if (slots < kMinRingSlots) {
    return std::unexpected("ring needs at least " + std::to_string(kMinRingSlots) +
                           " slots of " + std::to_string(kRingSlotMiB) + " MiB (" +
                           std::to_string(kMinRingSlots * kRingSlotMiB) + " MiB); " +
                           std::to_string(mib) + " MiB is " + std::to_string(slots));
  }
  if (slots > std::numeric_limits<uint32_t>::max()) {
    return std::unexpected(std::to_string(mib) + " MiB is " + std::to_string(slots) +
                           " slots, more than ring can count (" +
                           std::to_string(std::numeric_limits<uint32_t>::max()) + ")");
  }
  return RingGeometry{static_cast<uint32_t>(slots), kRingSlotMiB * kMiB};
}

std::expected<uint64_t, std::string> ringMappingBytes(const RingGeometry& geometry) {
  namespace layout = bitoscope::bulk_ring::layout;
  if (geometry.slot_count == 0 || geometry.slot_payload_bytes == 0) {
    return std::unexpected(std::string("ring needs at least one slot of at least one byte"));
  }
  constexpr uint64_t kMax = std::numeric_limits<uint64_t>::max();
  if (geometry.slot_payload_bytes > kMax - bitoscope::bulk_ring::kPayloadAlignment - layout::kSlotFixedBytes) {
    return std::unexpected("slot of " + std::to_string(geometry.slot_payload_bytes) +
                           " bytes does not fit in 64 bits");
  }
  const uint64_t stride = layout::slotStride(geometry.slot_payload_bytes);
  if (stride > (kMax - sizeof(layout::ControlBlock)) / geometry.slot_count) {
    return std::unexpected(std::to_string(geometry.slot_count) + " slots of " +
                           std::to_string(geometry.slot_payload_bytes) +
                           " bytes do not fit in 64 bits");
  }
  return layout::slotOffset(geometry.slot_payload_bytes, geometry.slot_count);
}

std::expected<uint64_t, std::string> acquisitionHeldBackBytes(
    uint64_t capture_held_back, const std::optional<RingGeometry>& ring) {
  if (!ring) return capture_held_back;
  auto ring_bytes = ringMappingBytes(*ring);
  if (!ring_bytes) return std::unexpected("cannot size bulk ring: " + ring_bytes.error());
  constexpr uint64_t kMax = std::numeric_limits<uint64_t>::max();
  return capture_held_back > kMax - *ring_bytes ? kMax : capture_held_back + *ring_bytes;
}

uint64_t captureBytesPerSecond(uint32_t channels, uint64_t sample_rate_milli_hz) {
  constexpr uint64_t kMax = std::numeric_limits<uint64_t>::max();
  constexpr uint64_t kMilliHzBitsPerByte = 8000;
  if (channels != 0 && sample_rate_milli_hz > kMax / channels) return kMax;
  return uint64_t{channels} * sample_rate_milli_hz / kMilliHzBitsPerByte;
}

uint32_t defaultPackWorkers(uint32_t hardware_threads, uint64_t bytes_per_second) {
  const uint32_t machine = defaultClearingWorkers(hardware_threads);
  const uint64_t per_rate = bytes_per_second / kPackWorkerBytesPerSecond +
                            (bytes_per_second % kPackWorkerBytesPerSecond != 0 ? 1 : 0) + 1;
  const uint64_t wanted = (std::max)(per_rate, uint64_t{kMinPackWorkers});
  return static_cast<uint32_t>((std::min)(wanted, uint64_t{machine}));
}

uint32_t defaultClearingWorkers(uint32_t hardware_threads) { return hardware_threads > 3 ? hardware_threads - 2 : 1; }

}
