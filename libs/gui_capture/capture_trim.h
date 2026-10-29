/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <expected>
#include <optional>
#include <string>

#include "capture_extent.h"
#include "capture_outcome.h"

namespace bitoscope::gui_capture {

enum class TrimSide : uint8_t { Before, After };

std::optional<bitoscope::capture::CaptureExtent> extentAfterTrim(bitoscope::capture::CaptureExtent extent, TrimSide side,
                                                                 uint64_t at);

std::expected<CaptureOutcome, std::string> trimmedOutcome(const CaptureOutcome& kept, TrimSide side, uint64_t at);

}
