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

std::optional<CursorPair> cursorsAfterTrim(const CursorPair& cursors, uint64_t first, uint64_t count) {
  const auto inside = [&](uint64_t sample) { return sample >= first && sample - first < count; };
  if (!inside(cursors.first()) || !inside(cursors.second())) return std::nullopt;
  return CursorPair(cursors.first() - first, cursors.second() - first);
}

}
