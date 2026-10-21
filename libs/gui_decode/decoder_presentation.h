/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "decoder_config.h"
#include "decoder_registry.h"

namespace bitoscope::gui_decode {

std::string presentationText(const DecoderConfig& config);

void applyPresentation(DecoderConfig& config, std::string_view text,
                       const bitoscope::decode::DecoderRegistry& registry);

std::vector<std::string> decoderPresentationOf(std::span<const DecoderConfig> configs,
                                               const bitoscope::decode::DecoderRegistry& registry);

void applyLoadedPresentations(std::span<DecoderConfig> configs, std::span<const std::optional<std::size_t>> entries,
                              std::span<const std::string> presentation,
                              const bitoscope::decode::DecoderRegistry& registry);

}
