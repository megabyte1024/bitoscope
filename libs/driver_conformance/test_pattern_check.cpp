/*
 * SPDX-License-Identifier: MIT
 */

#include "test_pattern_check.h"

#include <algorithm>

#include "pulse_runs.h"

namespace bitoscope::driver_conformance {

namespace {

sim::Runs counterRuns(uint32_t physical_channel, uint64_t num_samples) {
  const uint64_t half_period = uint64_t{1} << physical_channel;
  sim::Runs runs;
  bool level = false;
  for (uint64_t at = 0; at < num_samples; at += half_period) {
    runs.push_back(sim::Run{level, (std::min)(half_period, num_samples - at)});
    level = !level;
  }
  return runs;
}

}

std::optional<std::string> testPatternDifference(uint32_t physical_channel, std::span<const uint8_t> channel_bits,
                                                 uint64_t num_samples) {
  auto actual = sim::runsOfChannelBytes(channel_bits, num_samples);
  if (!actual) return actual.error();
  const auto difference = sim::firstDifference(physical_channel, counterRuns(physical_channel, num_samples), *actual);
  if (!difference) return std::nullopt;
  return difference->describe();
}

}
