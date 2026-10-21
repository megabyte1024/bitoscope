/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <string>
#include <string_view>

namespace bitoscope::log {

std::string escapeControlChars(std::string_view text);

}
