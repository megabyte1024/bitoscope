/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

// WHY: Token allows only ASCII letters, digits, '_' and '-': a backslash or a dot could escape the pipe namespace.

#include <expected>
#include <string>
#include <string_view>

namespace bitoscope::gui_test_input {

std::expected<std::string, std::string> pipeNameForToken(std::string_view token);

}
