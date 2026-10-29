/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>

namespace bitoscope::trigger {

struct PrePostSplit {
  uint64_t pre = 0;
  uint64_t post = 0;
  bool pre_was_whole_length = false;
};

uint64_t preTriggerSamplesForRatio(uint64_t length_samples, uint32_t percent);

PrePostSplit splitPreTrigger(uint64_t length_samples, uint64_t pre_samples);

}
