/*
 * SPDX-License-Identifier: MIT
 */

#include "trigger_split.h"

namespace bitoscope::trigger {

uint64_t preTriggerSamplesForRatio(uint64_t length_samples, uint32_t percent) {
  // WHY: Compute floor(L * ratio / 100) as L = 100 q + r so that L * ratio cannot overflow.
  return length_samples / 100 * percent + length_samples % 100 * percent / 100;
}

PrePostSplit splitPreTrigger(uint64_t length_samples, uint64_t pre_samples) {
  if (pre_samples == length_samples) return PrePostSplit{length_samples - 1, 1, true};
  return PrePostSplit{pre_samples, length_samples - pre_samples, false};
}

}
