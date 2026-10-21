/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <optional>

#include "waveform_source.h"

namespace bitoscope::waveform_view {

class Viewport;

struct PulseMeasurement {
  uint64_t startSample = 0;
  uint64_t midSample = 0;
  uint64_t endSample = 0;
  bool startedHigh = false;

  int64_t pointerHalfPicoseconds = 0;
  int64_t highTimePicoseconds = 0;
  int64_t lowTimePicoseconds = 0;
  int64_t periodPicoseconds = 0;
  uint64_t frequencyMilliHz = 0;
  uint32_t dutyBasisPoints = 0;

  double startPixelX = 0.0;
  double midPixelX = 0.0;
  double endPixelX = 0.0;
};

std::optional<PulseMeasurement> measurePulse(const WaveformSource& source, const Viewport& viewport,
                                              uint32_t channel, uint64_t sample);

}
