/*
 * SPDX-License-Identifier: MIT
 */

#include "cursors.h"

#include "fixed_time.h"

namespace bitoscope::waveform_view {

int64_t CursorPair::deltaTimePicoseconds(uint64_t sampleRateMilliHz) const {
  return sampleTimePicoseconds(static_cast<int64_t>(deltaSamples()), sampleRateMilliHz);
}

std::optional<uint64_t> CursorPair::deltaFrequencyMilliHz(uint64_t sampleRateMilliHz) const {
  const int64_t delta = deltaTimePicoseconds(sampleRateMilliHz);
  if (delta <= 0) return std::nullopt;
  return frequencyMilliHzFromPeriod(delta);
}

}
