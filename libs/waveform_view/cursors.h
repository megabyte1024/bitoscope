/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <optional>

namespace bitoscope::waveform_view {

class CursorPair {
 public:
  CursorPair(uint64_t first, uint64_t second) : first_(first), second_(second) {}

  uint64_t first() const { return first_; }
  uint64_t second() const { return second_; }

  uint64_t deltaSamples() const { return first_ > second_ ? first_ - second_ : second_ - first_; }

  int64_t deltaTimePicoseconds(uint64_t sampleRateMilliHz) const;

  std::optional<uint64_t> deltaFrequencyMilliHz(uint64_t sampleRateMilliHz) const;

 private:
  uint64_t first_;
  uint64_t second_;
};

std::optional<CursorPair> cursorsAfterTrim(const CursorPair& cursors, uint64_t first, uint64_t count);

}
