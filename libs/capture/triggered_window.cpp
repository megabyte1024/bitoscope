/*
 * SPDX-License-Identifier: MIT
 */

#include "triggered_window.h"

namespace bitoscope::capture {

uint32_t triggeredWindowSamples(uint64_t rate_milli_hz) {
  uint64_t window = kTriggeredWindowMinSamples;
  while (window < kTriggeredWindowMaxSamples && rate_milli_hz >= 750'000 * window) {
    window *= 2;
  }
  return static_cast<uint32_t>(window);
}

}
