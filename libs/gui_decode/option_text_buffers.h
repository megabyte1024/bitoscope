/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <array>
#include <cstddef>
#include <functional>
#include <map>
#include <span>
#include <string>
#include <string_view>

#include "decoder_config.h"

namespace bitoscope::gui_decode {

using OptionTextBuffer = std::array<char, 96>;
using OptionTextBuffers = std::map<std::string, OptionTextBuffer, std::less<>>;

OptionTextBuffer& optionTextBuffer(OptionTextBuffers& buffers, std::span<const DecoderConfig> configs,
                                   std::size_t index, std::string_view option, std::string_view value);

void pruneOptionTextBuffers(OptionTextBuffers& buffers, std::span<const DecoderConfig> configs);

}
