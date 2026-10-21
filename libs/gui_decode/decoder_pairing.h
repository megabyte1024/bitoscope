/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <span>
#include <string>
#include <vector>

#include "decoder_config.h"
#include "decoder_registry.h"

namespace bitoscope::gui_decode {

std::map<std::string, std::string, std::less<>> autoPairInputs(std::span<const DecoderConfig> configs,
                                                               std::size_t newIndex,
                                                               const bitoscope::decode::DecoderRegistry& registry);

std::size_t addDecoder(std::vector<DecoderConfig>& configs, const std::string& decoderName,
                       const bitoscope::decode::DecoderRegistry& registry, std::uint32_t* nextColor = nullptr);

}
