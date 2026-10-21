/*
 * SPDX-License-Identifier: MIT
 */

#include <algorithm>
#include <limits>

#include "raw_capture_buffer.h"

namespace bitoscope::capture {

uint64_t captureIndexWindows(const CapturePlan& plan, uint64_t arena_bytes) {
  const uint64_t arena_bound = plan.channels() == 0 ? 0 : arena_bytes / plan.channels();
  return (std::min)(plan.slots(), (std::max)(arena_bound, uint64_t{1}));
}

uint64_t captureIndexBytes(uint64_t windows, uint32_t channels) {
  constexpr uint64_t kMax = (std::numeric_limits<uint64_t>::max)();
  const uint64_t per_window = sizeof(IndexRecord) + uint64_t{channels} * sizeof(ChannelSegment);
  return windows != 0 && per_window > kMax / windows ? kMax : windows * per_window;
}

}
