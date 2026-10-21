/*
 * SPDX-License-Identifier: MIT
 */

#include "pacing_policy.h"

#include <algorithm>

namespace bitoscope::decoder_host {

uint64_t progressStepFor(const PacingPolicy& policy, uint64_t job_samples) {
  const uint64_t reports = (std::max)(policy.reports_per_decode, uint64_t{1});
  return (std::max)({policy.min_step_samples, job_samples / reports, uint64_t{1}});
}

const PacingPolicy& defaultPacingPolicy() {
  static const PacingPolicy policy;
  return policy;
}

}
