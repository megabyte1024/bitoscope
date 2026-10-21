/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <expected>
#include <string>
#include <string_view>

#include "bitoscope_decoder_cpp.h"

namespace bitoscope::decoders_common {

std::expected<plugin::StreamWriter, std::string> outputWriter(const plugin::DecodeJob& job, plugin::Annotations& out,
                                                              std::string_view name);

}
