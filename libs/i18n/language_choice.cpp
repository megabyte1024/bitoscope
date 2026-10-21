/*
 * SPDX-License-Identifier: MIT
 */

#include "language_choice.h"

#include <cctype>
#include <string_view>
#include <vector>

namespace bitoscope::i18n {

namespace {

std::string normalize(std::string_view tag) {
  std::string out;
  out.reserve(tag.size());
  for (char c : tag) {
    if (c == '-') {
      out.push_back('_');
    } else {
      out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
  }
  return out;
}

std::string_view languagePart(std::string_view normalized_tag) {
  std::size_t pos = normalized_tag.find('_');
  return pos == std::string_view::npos ? normalized_tag : normalized_tag.substr(0, pos);
}

}

std::string chooseLanguage(std::span<const std::string> preferred_tags,
              std::span<const std::string> available_catalogs) {
  std::vector<std::string> normalized_catalogs;
  normalized_catalogs.reserve(available_catalogs.size());
  for (const auto& catalog : available_catalogs) {
    normalized_catalogs.push_back(normalize(catalog));
  }

  // WHY: For each preferred tag in order: exact match, then language-only, before the next tag (owner decision).
  for (const auto& tag : preferred_tags) {
    // WHY: normalized_tag must be a named string: languagePart() returns a view that would dangle on a temporary.
    std::string normalized_tag = normalize(tag);
    for (std::size_t i = 0; i < available_catalogs.size(); ++i) {
      if (normalized_catalogs[i] == normalized_tag) {
        return available_catalogs[i];
      }
    }
    std::string_view tag_lang = languagePart(normalized_tag);
    for (std::size_t i = 0; i < available_catalogs.size(); ++i) {
      if (languagePart(normalized_catalogs[i]) == tag_lang) {
        return available_catalogs[i];
      }
    }
  }

  return "en";
}

std::string resolveLanguage(std::string_view preference, std::span<const std::string> preferred_tags,
                            std::span<const std::string> available_catalogs) {
  if (!preference.empty()) {
    if (preference == kSourceLanguage) {
      return std::string(preference);
    }
    for (const auto& catalog : available_catalogs) {
      if (catalog == preference) {
        return catalog;
      }
    }
  }
  std::vector<std::string> languages(available_catalogs.begin(), available_catalogs.end());
  languages.emplace_back(kSourceLanguage);
  return chooseLanguage(preferred_tags, languages);
}

}
