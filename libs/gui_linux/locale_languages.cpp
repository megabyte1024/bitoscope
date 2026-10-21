/*
 * SPDX-License-Identifier: MIT
 */

#include "locale_languages.h"

#include <algorithm>

namespace bitoscope::gui_linux {

namespace {

bool isAsciiLetter(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

bool isAsciiDigit(char c) {
  return c >= '0' && c <= '9';
}

char lower(char c) {
  return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

char upper(char c) {
  return (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c;
}

std::optional<std::string_view> setValue(const std::optional<std::string>& variable) {
  if (!variable || variable->empty()) {
    return std::nullopt;
  }
  return std::string_view(*variable);
}

void addOnce(std::vector<std::string>& tags, std::optional<std::string> tag) {
  if (tag && std::ranges::find(tags, *tag) == tags.end()) {
    tags.push_back(std::move(*tag));
  }
}

}

std::optional<std::string> languageTagFromLocale(std::string_view locale) {
  locale = locale.substr(0, locale.find_first_of(".@"));
  const std::size_t underscore = locale.find('_');
  const std::string_view language = locale.substr(0, underscore);
  if (language.size() < 2 || language.size() > 3 || !std::ranges::all_of(language, isAsciiLetter)) {
    return std::nullopt;
  }
  std::string tag;
  for (const char c : language) {
    tag += lower(c);
  }
  if (underscore == std::string_view::npos) {
    return tag;
  }
  const std::string_view territory = locale.substr(underscore + 1);
  const bool letters = territory.size() == 2 && std::ranges::all_of(territory, isAsciiLetter);
  const bool digits = territory.size() == 3 && std::ranges::all_of(territory, isAsciiDigit);
  if (!letters && !digits) {
    return std::nullopt;
  }
  tag += '-';
  for (const char c : territory) {
    tag += upper(c);
  }
  return tag;
}

std::vector<std::string> preferredLanguageTags(const LocaleEnvironment& environment) {
  std::optional<std::string_view> messages = setValue(environment.lcAll);
  if (!messages) {
    messages = setValue(environment.lcMessages);
  }
  if (!messages) {
    messages = setValue(environment.lang);
  }
  // WHY: Locale C or POSIX ignores LANGUAGE and prefers no language, as gettext does.
  if (!messages || *messages == "C" || *messages == "POSIX") {
    return {};
  }
  std::vector<std::string> tags;
  if (const std::optional<std::string_view> list = setValue(environment.language)) {
    std::string_view rest = *list;
    for (;;) {
      const std::size_t colon = rest.find(':');
      addOnce(tags, languageTagFromLocale(rest.substr(0, colon)));
      if (colon == std::string_view::npos) {
        break;
      }
      rest.remove_prefix(colon + 1);
    }
    return tags;
  }
  addOnce(tags, languageTagFromLocale(*messages));
  return tags;
}

}
