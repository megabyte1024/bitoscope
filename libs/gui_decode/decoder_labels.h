/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "decoder.h"
#include "decoder_config.h"
#include "decoder_registry.h"
#include "stream.h"

namespace bitoscope::gui_decode {

std::string decoderDisplay(const bitoscope::decode::DecoderRegistry& registry, std::string_view decoderName);

std::vector<std::string> decoderChoiceLabels(const bitoscope::decode::DecoderRegistry& registry);

std::string decoderListName(std::span<const DecoderConfig> configs, std::size_t index,
                            const bitoscope::decode::DecoderRegistry& registry);

std::string displayInstanceName(std::string_view id, std::string_view decoderName);

std::string storedInstanceId(std::string_view typed, std::string_view decoderName);

std::string decoderBarName(std::span<const DecoderConfig> configs, std::size_t index,
                           const bitoscope::decode::DecoderRegistry& registry);


std::string humanLabel(std::string_view id, std::string_view display);

std::string channelLabel(const bitoscope::decode::ChannelSpec& spec);
std::string inputLabel(const bitoscope::decode::InputSpec& spec);
std::string optionLabel(const bitoscope::decode::OptionSpec& spec);

std::string channelTooltip(const bitoscope::decode::ChannelSpec& spec);
std::string optionTooltip(const bitoscope::decode::OptionSpec& spec);
std::string inputTooltip(const bitoscope::decode::InputSpec& spec);

std::string bindingLabel(std::string_view binding, std::span<const DecoderConfig> configs,
                         const bitoscope::decode::DecoderRegistry& registry);

std::string decoderLaneLabel(const DecoderConfig& config, std::span<const DecoderConfig> configs,
                             const bitoscope::decode::DecoderRegistry& registry);

}
