/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace bitoscope::gui_linux {

std::string configHome(std::optional<std::string_view> xdgConfigHome,
                       std::optional<std::string_view> home);

std::string configDirectory(std::optional<std::string_view> xdgConfigHome,
                            std::optional<std::string_view> home);

}
