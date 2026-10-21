/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <expected>
#include <optional>
#include <string>
#include <string_view>

namespace bitoscope::decode {

class Decoder;
struct OutputSpec;

bool protocolAllows(const Decoder& reader, std::string_view carried);

std::optional<std::string> resolveOutputCarries(const Decoder& producer, const OutputSpec& output);

std::string outputCarries(const Decoder& producer, const OutputSpec& output);

std::expected<void, std::string> checkProtocolFit(const Decoder& reader, std::string_view binding,
                                                  const Decoder& producer, const OutputSpec& output);

}
