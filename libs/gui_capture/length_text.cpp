/*
 * SPDX-License-Identifier: MIT
 */

#include "length_text.h"

#include <cstddef>
#include <format>
#include <limits>

namespace bitoscope::gui_capture {

namespace {

constexpr uint64_t kMax = (std::numeric_limits<uint64_t>::max)();

bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

}

std::expected<uint64_t, std::string> parseLengthSamples(std::string_view text) {
  while (!text.empty() && isSpace(text.front())) text.remove_prefix(1);
  while (!text.empty() && isSpace(text.back())) text.remove_suffix(1);
  if (text.empty()) return std::unexpected(std::string("length is empty"));

  std::string whole;
  std::string fraction;
  bool point = false;
  unsigned exponent = 0;
  bool suffix = false;
  for (const char c : text) {
    if (suffix) {
      if (isSpace(c)) continue;
      if (c == 'k' || c == 'K' || c == 'm' || c == 'M' || c == 'g' || c == 'G') {
        return std::unexpected(std::format("length has '{}' after its suffix", c));
      }
      return std::unexpected(std::format("length has '{}': only digits and suffix k, m or g are allowed", c));
    }
    if (c >= '0' && c <= '9') {
      (point ? fraction : whole).push_back(c);
    } else if (c == '.') {
      if (point) return std::unexpected(std::string("length has more than one decimal point"));
      point = true;
    } else if (c == '\'' || c == ',' || c == '_' || isSpace(c)) {
      continue;
    } else if (c == 'k' || c == 'K') {
      suffix = true;
      exponent = 3;
    } else if (c == 'm' || c == 'M') {
      suffix = true;
      exponent = 6;
    } else if (c == 'g' || c == 'G') {
      suffix = true;
      exponent = 9;
    } else {
      return std::unexpected(std::format("length has '{}': only digits and suffix k, m or g are allowed", c));
    }
  }
  if (whole.empty() && fraction.empty()) return std::unexpected(std::string("length has no digits"));

  const auto tooBig = [] { return std::unexpected(std::string("length does not fit in 64 bits")); };
  uint64_t value = 0;
  for (const char c : whole) {
    const uint64_t digit = static_cast<uint64_t>(c - '0');
    if (value > (kMax - digit) / 10) return tooBig();
    value = value * 10 + digit;
  }
  uint64_t scale = 1;
  for (unsigned i = 0; i < exponent; ++i) scale *= 10;
  if (value != 0 && value > kMax / scale) return tooBig();
  value *= scale;

  uint64_t fractionSamples = 0;
  for (std::size_t i = 0; i < fraction.size(); ++i) {
    const uint64_t digit = static_cast<uint64_t>(fraction[i] - '0');
    if (i < exponent) {
      fractionSamples = fractionSamples * 10 + digit;
    } else if (digit != 0) {
      return std::unexpected(std::string("length is not whole number of samples"));
    }
  }
  for (std::size_t i = fraction.size(); i < exponent; ++i) fractionSamples *= 10;
  if (value > kMax - fractionSamples) return tooBig();
  value += fractionSamples;

  if (value == 0) return std::unexpected(std::string("length is zero"));
  return value;
}

}
