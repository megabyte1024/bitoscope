/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>

namespace bitoscope::waveform_view {

uint64_t mulDivFloor(uint64_t a, uint64_t b, uint64_t c);

uint64_t mulDivRound(uint64_t a, uint64_t b, uint64_t c);

int64_t sampleTimePicoseconds(int64_t sample, uint64_t sampleRateMilliHz);

int64_t samplesFromPicoseconds(int64_t picoseconds, uint64_t sampleRateMilliHz);

uint64_t frequencyMilliHzFromPeriod(int64_t periodPicoseconds);

uint32_t dutyBasisPoints(int64_t highPicoseconds, int64_t periodPicoseconds);

}
