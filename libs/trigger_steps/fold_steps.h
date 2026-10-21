/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <span>

namespace bitoscope::trigger {

enum class StepTest { kHigh, kLow, kRising, kFalling, kEither };
enum class StepCombine { kAnd, kOr };

uint64_t foldWholeSteps(StepTest test, StepCombine combine, std::span<const uint8_t> step_bytes, bool sample_before,
                        std::span<uint64_t> combined);

}
