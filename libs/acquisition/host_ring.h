/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <string>

#include "bulk_ring.h"
#include "ring_config.h"

namespace bitoscope::acquisition {

using CreateRingFn = std::function<std::expected<std::unique_ptr<bitoscope::bulk_ring::BulkRing>,
                                                 std::string>(const RingGeometry& geometry,
                                                              uint64_t capture_id)>;

std::expected<std::unique_ptr<bitoscope::bulk_ring::BulkRing>, std::string> createHostRing(
    const RingGeometry& geometry, uint64_t capture_id);

uint64_t randomCaptureId();

}
