/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <expected>
#include <optional>
#include <string>

#include "decode_threads.h"
#include "decoder_runner.h"
#include "work_placement.h"

namespace bitoscope::gui_decode {

struct DecodePlacement {
  cpu::WorkPlacement work;
  std::string note;
};

DecodePlacement decodePlacementFor(const cpu::ProcessorLayout& layout, std::optional<uint32_t> count = std::nullopt);

std::expected<DecodePlacement, std::string> decodePlacementOfThisMachine(std::optional<uint32_t> count = std::nullopt);

DecodeThreadSetup decodeThreadSetup(const DecodePlacement& placement);

}
