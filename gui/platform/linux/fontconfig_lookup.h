/*
 * SPDX-License-Identifier: MIT
 */

// WHY: fontconfig is loaded with dlopen, not linked: its development headers are not a build prerequisite.
#pragma once

#include <expected>
#include <filesystem>
#include <string>
#include <string_view>

namespace bitoscope::gui::linux_platform {

struct FontMatch {
  std::filesystem::path file;
  int index = 0;
  std::string family;
};

std::expected<FontMatch, std::string> matchFontFamily(std::string_view family);

}
