/*
 * SPDX-License-Identifier: MIT
 */

#include "option_text.h"

#include <algorithm>
#include <cctype>

namespace bitoscope::decode {

namespace {

bool allDigits(std::string_view s) {
  return std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isdigit(c) != 0; });
}

}

std::string canonicalOptionText(std::string_view text) {
  const std::size_t dot = text.find('.');
  if (dot == std::string_view::npos || dot == 0) return std::string(text);
  const std::string_view whole = text.substr(0, dot);
  std::string_view fraction = text.substr(dot + 1);
  if (!allDigits(whole) || !allDigits(fraction)) return std::string(text);
  while (!fraction.empty() && fraction.back() == '0') fraction.remove_suffix(1);
  return fraction.empty() ? std::string(whole) : std::string(whole) + "." + std::string(fraction);
}

}
