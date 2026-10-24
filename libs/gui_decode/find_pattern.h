/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace bitoscope::gui_decode {

enum class FindMode { Ascii, Hex };

enum class FindPatternError { Empty, OddHexDigits, NotHex, NotAscii };

std::expected<std::vector<std::uint8_t>, FindPatternError> parseFindPattern(std::string_view text, FindMode mode);

bool acceptsFindChar(FindMode mode, unsigned int c);

std::optional<std::string> convertFindText(std::string_view text, FindMode from, FindMode to);

}
