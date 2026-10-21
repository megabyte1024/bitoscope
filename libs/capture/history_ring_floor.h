/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

// WHY: Do not size the ring by guaranteedSamples(): the linear floor is not safe once segments wrap past the head.

#include <cstdint>

namespace bitoscope::capture {

class CapturePlan;
class Reservation;

uint64_t historyRingWindows(const Reservation& reservation);

uint64_t historyRingSamples(const Reservation& reservation);

uint64_t triggeredArenaCap(const CapturePlan& plan, uint64_t block_bytes);

}
