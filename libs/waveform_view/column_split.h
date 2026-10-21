/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>

namespace bitoscope::waveform_view {

constexpr uint64_t columnBoundary(uint64_t begin, uint64_t end, uint64_t columns, uint64_t j) {
  const uint64_t range = end - begin;
  const uint64_t q = range / columns;
  const uint64_t r = range % columns;
  return begin + q * j + r * j / columns;
}

}
