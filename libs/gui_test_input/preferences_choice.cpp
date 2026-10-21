/*
 * SPDX-License-Identifier: MIT
 */

#include "preferences_choice.h"

#include <format>

namespace bitoscope::gui_test_input {

namespace {

std::filesystem::path pathFromUtf8(std::string_view text) {
  return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
}

}

PreferencesChoice choosePreferencesFile(bool hookActive, std::optional<std::string_view> overrideValue,
                                        const std::filesystem::path& defaultFile) {
  PreferencesChoice choice;
  choice.file = defaultFile;
  if (!overrideValue) {
    return choice;
  }
  if (!hookActive) {
    choice.note = "BITOSCOPE_PREFERENCES is ignored unless test hook is on (BITOSCOPE_TEST_INPUT)";
    return choice;
  }
  if (overrideValue->empty()) {
    choice.file.clear();
    choice.note = "BITOSCOPE_PREFERENCES is set but empty: no preferences file is read or saved";
    return choice;
  }
  std::filesystem::path wanted = pathFromUtf8(*overrideValue);
  if (!wanted.is_absolute()) {
    choice.file.clear();
    choice.note = std::format(
      "BITOSCOPE_PREFERENCES must be absolute path, got '{}': no preferences file is read or saved",
      *overrideValue);
    return choice;
  }
  choice.file = std::move(wanted);
  choice.note = std::format("BITOSCOPE_PREFERENCES: preferences are read from and saved to {}", *overrideValue);
  return choice;
}

}
