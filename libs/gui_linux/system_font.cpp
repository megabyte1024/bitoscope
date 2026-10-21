/*
 * SPDX-License-Identifier: MIT
 */

#include "system_font.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <vector>

namespace bitoscope::gui_linux {

namespace {

bool isSpace(char c) {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

std::string_view trim(std::string_view text) {
  while (!text.empty() && isSpace(text.front())) {
    text.remove_prefix(1);
  }
  while (!text.empty() && isSpace(text.back())) {
    text.remove_suffix(1);
  }
  return text;
}

std::string lowerAscii(std::string_view text) {
  std::string out(text);
  std::ranges::transform(out, out.begin(), [](char c) {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  });
  return out;
}

bool isPangoStyleWord(std::string_view word) {
  static constexpr std::array<std::string_view, 50> kWords{
    "normal",          "roman",          "oblique",        "italic",         "small-caps",
    "all-small-caps",  "petite-caps",    "all-petite-caps", "unicase",       "title-caps",
    "thin",            "ultra-light",    "ultralight",     "extra-light",    "extralight",
    "light",           "semi-light",     "semilight",      "demi-light",     "demilight",
    "book",            "regular",        "medium",         "semi-bold",      "semibold",
    "demi-bold",       "demibold",       "bold",           "ultra-bold",     "ultrabold",
    "extra-bold",      "extrabold",      "heavy",          "black",          "ultra-heavy",
    "ultraheavy",      "ultra-condensed", "extra-condensed", "condensed",    "semi-condensed",
    "semi-expanded",   "expanded",       "extra-expanded", "ultra-expanded", "not-rotated",
    "south",           "upside-down",    "north",          "rotated-left",   "rotated-right"};
  const std::string lower = lowerAscii(word);
  return std::ranges::find(kWords, std::string_view(lower)) != kWords.end() || lower == "east" ||
         lower == "west";
}

bool isPangoSize(std::string_view word) {
  if (word.ends_with("px")) {
    word.remove_suffix(2);
  }
  if (word.empty()) {
    return false;
  }
  bool digit = false;
  bool point = false;
  for (const char c : word) {
    if (c >= '0' && c <= '9') {
      digit = true;
    } else if (c == '.' && !point) {
      point = true;
    } else {
      return false;
    }
  }
  return digit;
}

std::vector<std::string_view> words(std::string_view text) {
  std::vector<std::string_view> out;
  std::size_t i = 0;
  while (i < text.size()) {
    while (i < text.size() && isSpace(text[i])) {
      ++i;
    }
    const std::size_t start = i;
    while (i < text.size() && !isSpace(text[i])) {
      ++i;
    }
    if (i > start) {
      out.push_back(text.substr(start, i - start));
    }
  }
  return out;
}

}

std::optional<std::string> pangoFontFamily(std::string_view description) {
  std::vector<std::string_view> tokens = words(description);
  if (!tokens.empty() && tokens.back().starts_with('@')) {
    tokens.pop_back();
  }
  if (!tokens.empty() && isPangoSize(tokens.back())) {
    tokens.pop_back();  // WHY: Drop one size only: a family may end in a number itself.
  }
  while (!tokens.empty() && isPangoStyleWord(tokens.back())) {
    tokens.pop_back();
  }
  if (tokens.empty()) {
    return std::nullopt;
  }
  const char* begin = tokens.front().data();
  const char* end = tokens.back().data() + tokens.back().size();
  std::string_view families(begin, static_cast<std::size_t>(end - begin));
  const std::string_view first = trim(families.substr(0, families.find(',')));
  if (first.empty()) {
    return std::nullopt;
  }
  return std::string(first);
}

std::optional<std::string> kdeglobalsFontFamily(std::string_view kdeglobals) {
  bool inGeneral = false;
  std::optional<std::string> family;
  while (!kdeglobals.empty()) {
    const std::size_t newline = kdeglobals.find('\n');
    std::string_view line = trim(kdeglobals.substr(0, newline));
    kdeglobals.remove_prefix(newline == std::string_view::npos ? kdeglobals.size() : newline + 1);
    if (line.empty() || line.front() == '#') {
      continue;
    }
    if (line.front() == '[') {
      inGeneral = line.starts_with("[General]");
      continue;
    }
    const std::size_t equals = line.find('=');
    if (!inGeneral || equals == std::string_view::npos) {
      continue;
    }
    std::string_view key = trim(line.substr(0, equals));
    if (const std::size_t bracket = key.find('['); bracket != std::string_view::npos) {
      if (key.substr(bracket).starts_with("[$")) {
        key = key.substr(0, bracket);
      } else {
        continue;
      }
    }
    if (key != "font") {
      continue;
    }
    const std::string_view value = trim(line.substr(equals + 1));
    const std::string_view name = trim(value.substr(0, value.find(',')));
    family = name.empty() ? std::nullopt : std::optional<std::string>(name);
  }
  return family;
}

std::string systemFontFamily(std::optional<std::string_view> portalFontName,
                             std::optional<std::string_view> kdeglobals) {
  if (portalFontName) {
    if (std::optional<std::string> family = pangoFontFamily(*portalFontName)) {
      return *family;
    }
  }
  if (kdeglobals) {
    if (std::optional<std::string> family = kdeglobalsFontFamily(*kdeglobals)) {
      return *family;
    }
  }
  return "sans-serif";
}

}
