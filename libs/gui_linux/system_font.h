/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace bitoscope::gui_linux {

std::optional<std::string> pangoFontFamily(std::string_view description);

std::optional<std::string> kdeglobalsFontFamily(std::string_view kdeglobals);

std::string systemFontFamily(std::optional<std::string_view> portalFontName,
                             std::optional<std::string_view> kdeglobals);

}
