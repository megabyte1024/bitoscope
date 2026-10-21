/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <expected>
#include <string>
#include <string_view>

#include "test_command.h"

namespace bitoscope::gui_test_input {

std::expected<KeyChord, std::string> parseKeyChord(std::string_view text);

std::string_view keyName(KeyId key);

}
