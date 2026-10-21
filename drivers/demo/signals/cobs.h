/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace bitoscope::sim {

std::vector<uint8_t> encodeCobs(std::span<const uint8_t> data);

}
