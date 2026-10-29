/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace bitoscope::driver_conformance {

std::optional<std::string> testPatternDifference(uint32_t physical_channel, std::span<const uint8_t> channel_bits,
                                                 uint64_t num_samples);

}
