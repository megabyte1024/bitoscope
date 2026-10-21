/*
 * SPDX-License-Identifier: MIT
 */

#include "fold_steps.h"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstring>

namespace bitoscope::trigger {

namespace {

constexpr size_t kStepBytes = 8;

uint64_t loadStep(const uint8_t* bytes) {
  uint64_t step = 0;
  std::memcpy(&step, bytes, kStepBytes);
  if constexpr (std::endian::native == std::endian::big) {
    step = std::byteswap(step);
  }
  return step;
}

template <StepTest kTest>
uint64_t contribution(uint64_t step, uint64_t sample_before) {
  if constexpr (kTest == StepTest::kHigh) {
    return step;
  } else if constexpr (kTest == StepTest::kLow) {
    return ~step;
  } else {
    const uint64_t edge = step ^ ((step << 1) | sample_before);
    if constexpr (kTest == StepTest::kRising) {
      return edge & step;
    } else if constexpr (kTest == StepTest::kFalling) {
      return edge & ~step;
    } else {
      return edge;
    }
  }
}

template <StepTest kTest, StepCombine kCombine>
uint64_t fold(const uint8_t* bytes, uint64_t sample_before, uint64_t* combined, size_t steps) {
  uint64_t any = 0;
  for (size_t k = 0; k < steps; ++k) {
    const uint64_t step = loadStep(bytes + k * kStepBytes);
    const uint64_t c = contribution<kTest>(step, sample_before);
    combined[k] = (kCombine == StepCombine::kAnd) ? (combined[k] & c) : (combined[k] | c);
    any |= combined[k];
    sample_before = step >> 63;
  }
  return any;
}

template <StepCombine kCombine>
uint64_t foldAs(StepTest test, const uint8_t* bytes, uint64_t sample_before, uint64_t* combined, size_t steps) {
  switch (test) {
    case StepTest::kHigh:
      return fold<StepTest::kHigh, kCombine>(bytes, sample_before, combined, steps);
    case StepTest::kLow:
      return fold<StepTest::kLow, kCombine>(bytes, sample_before, combined, steps);
    case StepTest::kRising:
      return fold<StepTest::kRising, kCombine>(bytes, sample_before, combined, steps);
    case StepTest::kFalling:
      return fold<StepTest::kFalling, kCombine>(bytes, sample_before, combined, steps);
    case StepTest::kEither:
      return fold<StepTest::kEither, kCombine>(bytes, sample_before, combined, steps);
  }
  return 0;
}

}

uint64_t foldWholeSteps(StepTest test, StepCombine combine, std::span<const uint8_t> step_bytes, bool sample_before,
                        std::span<uint64_t> combined) {
  const size_t steps = std::min(combined.size(), step_bytes.size() / kStepBytes);
  const uint64_t before = sample_before ? 1 : 0;
  if (combine == StepCombine::kAnd) {
    return foldAs<StepCombine::kAnd>(test, step_bytes.data(), before, combined.data(), steps);
  }
  return foldAs<StepCombine::kOr>(test, step_bytes.data(), before, combined.data(), steps);
}

}
