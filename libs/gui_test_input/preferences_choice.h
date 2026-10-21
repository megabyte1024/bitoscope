/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

// WHY: BITOSCOPE_PREFERENCES counts only while the test hook is on; an unusable value means no file at all,
// never the real one: a typo must not reach the owner's preferences.

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace bitoscope::gui_test_input {

struct PreferencesChoice {
  std::filesystem::path file;
  std::string note;
};

PreferencesChoice choosePreferencesFile(bool hookActive, std::optional<std::string_view> overrideValue,
                                        const std::filesystem::path& defaultFile);

}
