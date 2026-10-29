/*
 * SPDX-License-Identifier: MIT
 */

#include "pulse_measurement.h"

#include "fixed_time.h"
#include "viewport.h"

namespace bitoscope::waveform_view {

std::optional<PulseMeasurement> measurePulse(const WaveformSource& source, const Viewport& viewport,
                                              uint32_t channel, uint64_t sample) {
  if (channel >= source.channelCount() || sample >= source.sampleCount()) return std::nullopt;
  if (sample < source.summaryPendingBefore()) return std::nullopt;

  const std::optional<uint64_t> e0 = source.previousEdge(channel, sample);
  if (!e0.has_value()) return std::nullopt;
  const std::optional<uint64_t> e1 = source.nextEdge(channel, sample);
  if (!e1.has_value()) return std::nullopt;
  const std::optional<uint64_t> e2 = source.nextEdge(channel, *e1);
  if (!e2.has_value()) return std::nullopt;

  const uint64_t rate = viewport.sampleRateMilliHz();
  const bool startedHigh = source.level(channel, sample);

  const int64_t firstWidthPs = sampleTimePicoseconds(static_cast<int64_t>(*e1 - *e0), rate);
  const int64_t secondWidthPs = sampleTimePicoseconds(static_cast<int64_t>(*e2 - *e1), rate);

  PulseMeasurement m;
  m.startSample = *e0;
  m.midSample = *e1;
  m.endSample = *e2;
  m.startedHigh = startedHigh;
  m.pointerHalfPicoseconds = firstWidthPs;
  m.highTimePicoseconds = startedHigh ? firstWidthPs : secondWidthPs;
  m.lowTimePicoseconds = startedHigh ? secondWidthPs : firstWidthPs;
  m.periodPicoseconds = sampleTimePicoseconds(static_cast<int64_t>(*e2 - *e0), rate);
  m.frequencyMilliHz = frequencyMilliHzFromPeriod(m.periodPicoseconds);
  m.dutyBasisPoints = bitoscope::waveform_view::dutyBasisPoints(m.highTimePicoseconds, m.periodPicoseconds);
  m.startPixelX = viewport.samplePixelX(static_cast<int64_t>(*e0));
  m.midPixelX = viewport.samplePixelX(static_cast<int64_t>(*e1));
  m.endPixelX = viewport.samplePixelX(static_cast<int64_t>(*e2));
  return m;
}

}
