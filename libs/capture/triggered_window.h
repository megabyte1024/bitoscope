/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

// WHY: Nearest power of two to rate x 2 ms, a tie going UP: bigger window means fewer windows a second to pack.

#include <cstdint>

namespace bitoscope::capture {

inline constexpr uint32_t kTriggeredWindowMinSamples = 8192;
inline constexpr uint32_t kTriggeredWindowMaxSamples = 1'048'576;

uint32_t triggeredWindowSamples(uint64_t rate_milli_hz);

}
