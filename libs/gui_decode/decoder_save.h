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
#include "decoder_visibility.h"

namespace bitoscope::gui_decode {

void recordLoadedSetup(std::span<DecoderConfig> configs, std::span<const std::optional<std::size_t>> entries,
                       const bitoscope::decode::DecoderRegistry& registry);

std::vector<std::optional<std::size_t>> unchangedFileEntries(std::span<const DecoderConfig> configs,
                                                             const bitoscope::decode::DecoderRegistry& registry);

std::vector<std::string> decoderLaneKeys(std::span<const DecoderConfig> configs);

std::vector<waveform_view::DecoderVisibility> decoderVisibilities(std::span<const DecoderConfig> configs);

std::expected<void, std::string> applyDecoderVisibility(std::span<DecoderConfig> configs, std::string_view text,
                                                        const bitoscope::decode::DecoderRegistry& registry);

}
