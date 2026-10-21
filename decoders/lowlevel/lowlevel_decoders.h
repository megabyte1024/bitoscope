/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <expected>
#include <memory>
#include <string>
#include <string_view>

#include "bitoscope_decoder_cpp.h"

namespace bitoscope::decoders_lowlevel {

std::unique_ptr<plugin::ProtocolDecoder> makeLowLevelDecoder(std::string_view name);

std::expected<void, std::string> addLowLevelDecoders(plugin::Decoders& decoders);

}
