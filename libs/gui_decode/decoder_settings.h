/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "decoder.h"
#include "decoder_config.h"
#include "decoder_registry.h"
#include "option_widget.h"

namespace bitoscope::gui_decode {

bool anyRequiredChannel(std::span<const bitoscope::decode::ChannelSpec> channels);

std::expected<bool, std::string> commitDecoderName(std::span<DecoderConfig> configs, std::size_t index,
                                                   std::string_view typed,
                                                   const bitoscope::decode::DecoderRegistry& registry);

std::string steppedIntegerText(const bitoscope::decode::OptionSpec& spec, std::string_view current,
                               std::int64_t delta);

std::size_t sliderStepCount(const OptionWidget& widget);
std::optional<std::size_t> sliderPosition(const OptionWidget& widget, std::string_view text);
std::string sliderText(const OptionWidget& widget, std::size_t position);

}
