/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

// WHY: payload is borrowed from a ring slot recycled once the call returns: never keep the span past the call.

#include <cstdint>
#include <span>

#include "sample_encoding.h"

namespace bitoscope::host {

struct ChunkView {
  uint64_t first_sample_index = 0;
  uint64_t sample_rate_milli_hz = 0;
  uint32_t num_channels = 0;
  uint32_t num_samples = 0;
  SampleEncoding encoding = SampleEncoding::Unspecified;
  uint64_t capture_id = 0;
  std::span<const uint8_t> payload;
};

}
