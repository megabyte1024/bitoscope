/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace bitoscope::gui_linux {

struct LocaleEnvironment {
  std::optional<std::string> language;
  std::optional<std::string> lcAll;
  std::optional<std::string> lcMessages;
  std::optional<std::string> lang;
};

std::optional<std::string> languageTagFromLocale(std::string_view locale);

std::vector<std::string> preferredLanguageTags(const LocaleEnvironment& environment);

}
