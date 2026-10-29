/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>

namespace bitoscope::capture {

struct CaptureExtent {
  uint64_t first_sample = 0;
  uint64_t count = 0;

  bool operator==(const CaptureExtent&) const = default;
};

enum class ExtentTail : uint8_t { Exact, InsideLastWindow, Trimmed };

}
