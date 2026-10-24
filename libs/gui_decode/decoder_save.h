/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstddef>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "decoder_config.h"
#include "decoder_registry.h"

namespace bitoscope::gui_decode {

void recordLoadedSetup(std::span<DecoderConfig> configs, std::span<const std::optional<std::size_t>> entries,
                       const bitoscope::decode::DecoderRegistry& registry);

std::vector<std::optional<std::size_t>> unchangedFileEntries(std::span<const DecoderConfig> configs,
                                                             const bitoscope::decode::DecoderRegistry& registry);

std::vector<std::string> decoderLaneKeys(std::span<const DecoderConfig> configs);

}
