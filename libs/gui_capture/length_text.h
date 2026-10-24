/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

namespace bitoscope::gui_capture {

std::expected<uint64_t, std::string> parseLength(std::string_view text, uint64_t sampleRateMilliHz);

}
