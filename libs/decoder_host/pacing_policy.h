/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>

namespace bitoscope::decoder_host {

struct PacingPolicy {
  uint32_t first_picture_ms = 16;
  uint32_t growth_percent = 25;
  uint32_t longest_wait_ms = 250;
  uint64_t reports_per_decode = 1000;
  uint64_t min_step_samples = 4096;
};

uint64_t progressStepFor(const PacingPolicy& policy, uint64_t job_samples);

const PacingPolicy& defaultPacingPolicy();

}
