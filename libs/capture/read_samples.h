/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <expected>
#include <span>
#include <string>

#include "raw_capture_buffer.h"

namespace bitoscope::capture {

std::expected<void, std::string> readSamples(const RawCaptureBuffer& buffer, uint32_t channel,
                                             uint64_t first_sample, uint64_t count,
                                             std::span<uint8_t> out);

}
