/*
 * SPDX-License-Identifier: MIT
 */

#include "fixed_time.h"

namespace bitoscope::waveform_view {

namespace {

// WHY: No __int128: MSVC has none, so the 128-bit product is built from 32-bit halves.
void mul64x64(uint64_t a, uint64_t b, uint64_t& hi, uint64_t& lo) {
  const uint64_t a_lo = static_cast<uint32_t>(a);
  const uint64_t a_hi = a >> 32;
  const uint64_t b_lo = static_cast<uint32_t>(b);
  const uint64_t b_hi = b >> 32;

  const uint64_t t0 = a_lo * b_lo;
  const uint64_t t1 = a_hi * b_lo + (t0 >> 32);
  const uint64_t t2 = a_lo * b_hi + static_cast<uint32_t>(t1);
  lo = (t2 << 32) | static_cast<uint32_t>(t0);
  hi = a_hi * b_hi + (t1 >> 32) + (t2 >> 32);
}

uint64_t divide128by64(uint64_t hi, uint64_t lo, uint64_t divisor) {
  uint64_t remHi = 0;
  uint64_t remLo = 0;
  uint64_t quotient = 0;
  for (int bit = 127; bit >= 0; --bit) {
    const uint64_t nextBit = (bit >= 64) ? ((hi >> (bit - 64)) & 1u) : ((lo >> bit) & 1u);
    remHi = (remHi << 1) | (remLo >> 63);
    remLo = (remLo << 1) | nextBit;
    quotient <<= 1;
    if (remHi > 0 || remLo >= divisor) {
      const uint64_t borrow = (remLo < divisor) ? 1u : 0u;
      remLo -= divisor;
      remHi -= borrow;
      quotient |= 1u;
    }
  }
  return quotient;
}

constexpr uint64_t kPsPerSecondTimesMilli = 1'000'000'000'000'000ULL;

}

uint64_t mulDivFloor(uint64_t a, uint64_t b, uint64_t c) {
  uint64_t hi = 0;
  uint64_t lo = 0;
  mul64x64(a, b, hi, lo);
  return divide128by64(hi, lo, c);
}

uint64_t mulDivRound(uint64_t a, uint64_t b, uint64_t c) {
  uint64_t hi = 0;
  uint64_t lo = 0;
  mul64x64(a, b, hi, lo);
  const uint64_t half = c / 2;
  const uint64_t newLo = lo + half;
  const uint64_t carry = (newLo < lo) ? 1u : 0u;
  return divide128by64(hi + carry, newLo, c);
}

int64_t sampleTimePicoseconds(int64_t sample, uint64_t sampleRateMilliHz) {
  if (sample >= 0) {
    return static_cast<int64_t>(mulDivFloor(static_cast<uint64_t>(sample), kPsPerSecondTimesMilli, sampleRateMilliHz));
  }
  return -static_cast<int64_t>(
      mulDivFloor(static_cast<uint64_t>(-sample), kPsPerSecondTimesMilli, sampleRateMilliHz));
}

int64_t samplesFromPicoseconds(int64_t picoseconds, uint64_t sampleRateMilliHz) {
  if (picoseconds >= 0) {
    return static_cast<int64_t>(mulDivFloor(static_cast<uint64_t>(picoseconds), sampleRateMilliHz, kPsPerSecondTimesMilli));
  }
  return -static_cast<int64_t>(
      mulDivFloor(static_cast<uint64_t>(-picoseconds), sampleRateMilliHz, kPsPerSecondTimesMilli));
}

uint64_t frequencyMilliHzFromPeriod(int64_t periodPicoseconds) {
  return mulDivRound(1, kPsPerSecondTimesMilli, static_cast<uint64_t>(periodPicoseconds));
}

uint32_t dutyBasisPoints(int64_t highPicoseconds, int64_t periodPicoseconds) {
  return static_cast<uint32_t>(
      mulDivRound(static_cast<uint64_t>(highPicoseconds), 10000, static_cast<uint64_t>(periodPicoseconds)));
}

}
