/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <expected>
#include <span>
#include <string>

namespace bitoscope::hdf5 {

std::expected<void, std::string> inflateChunk(std::span<const uint8_t> stored, bool deflated, std::span<uint8_t> out);

}
