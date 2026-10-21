/*
 * SPDX-License-Identifier: MIT
 */

#include "host_ring.h"

#include <format>
#include <new>
#include <random>
#include <utility>

namespace bitoscope::acquisition {

namespace br = bitoscope::bulk_ring;

std::expected<std::unique_ptr<br::BulkRing>, std::string> createHostRing(const RingGeometry& geometry,
                                                                        uint64_t capture_id) {
  try {
    auto ring = br::BulkRing::create(br::makeRingName(), geometry.slot_count,
                                     geometry.slot_payload_bytes, capture_id);
    if (!ring) return std::unexpected(ring.error());
    return std::make_unique<br::BulkRing>(std::move(*ring));
  } catch (const std::bad_alloc&) {
    return std::unexpected(std::format("out of memory making bulk ring of {} slots of {} bytes",
                                       geometry.slot_count, geometry.slot_payload_bytes));
  }
}

uint64_t randomCaptureId() {
  std::random_device entropy;
  while (true) {
    const uint64_t id = (static_cast<uint64_t>(entropy()) << 32) ^ entropy();
    if (id != 0) return id;
  }
}

}
