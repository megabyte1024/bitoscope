/*
 * SPDX-License-Identifier: MIT
 */

#include "length_text.h"

#include <cstddef>
#include <format>
#include <limits>
#include <optional>

namespace bitoscope::gui_capture {

namespace {

constexpr uint64_t kMax = (std::numeric_limits<uint64_t>::max)();

constexpr std::string_view kAllowed = "only digits, suffix k, m or g and unit s, ms, us or ns are allowed";
constexpr std::string_view kNoComma = "length has ',': digits are grouped only with ' (1'000'000), decimal point is '.'";
constexpr std::string_view kSpaceInside = "length has space inside number";

bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

char lower(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

std::unexpected<std::string> tooBig() { return std::unexpected(std::string("length does not fit in 64 bits")); }

std::expected<uint64_t, std::string> parseSamples(std::string_view text) {
  while (!text.empty() && isSpace(text.front())) text.remove_prefix(1);
  while (!text.empty() && isSpace(text.back())) text.remove_suffix(1);
  if (text.empty()) return std::unexpected(std::string("length is empty"));

  std::string whole;
  std::string fraction;
  bool point = false;
  unsigned exponent = 0;
  bool suffix = false;
  bool space = false;
  for (const char c : text) {
    if (suffix) {
      if (isSpace(c)) continue;
      if (c == 'k' || c == 'K' || c == 'm' || c == 'M' || c == 'g' || c == 'G') {
        return std::unexpected(std::format("length has '{}' after its suffix", c));
      }
      return std::unexpected(std::format("length has '{}': {}", c, kAllowed));
    }
    if (space && ((c >= '0' && c <= '9') || c == '.' || c == '\'')) return std::unexpected(std::string(kSpaceInside));
    if (c >= '0' && c <= '9') {
      (point ? fraction : whole).push_back(c);
    } else if (c == '.') {
      if (point) return std::unexpected(std::string("length has more than one decimal point"));
      point = true;
    } else if (c == '\'') {
      continue;
    } else if (c == ',') {
      return std::unexpected(std::string(kNoComma));
    } else if (isSpace(c)) {
      space = true;
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
      return std::unexpected(std::format("length has '{}': {}", c, kAllowed));
    }
  }
  if (whole.empty() && fraction.empty()) return std::unexpected(std::string("length has no digits"));

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

std::optional<uint64_t> stripTimeUnit(std::string_view& text) {
  if (text.empty() || lower(text.back()) != 's') return std::nullopt;
  text.remove_suffix(1);
  if (text.empty()) return 1;
  uint64_t perSecond = 1;
  switch (lower(text.back())) {
  case 'm': perSecond = 1'000; break;
  case 'u': perSecond = 1'000'000; break;
  case 'n': perSecond = 1'000'000'000; break;
  default: return 1;
  }
  text.remove_suffix(1);
  return perSecond;
}

// WHY: No __int128: MSVC has none, so the 128-bit product is built from 32-bit halves.
std::optional<uint64_t> mulDivCeil(uint64_t a, uint64_t b, uint64_t divisor) {
  const uint64_t aLo = static_cast<uint32_t>(a);
  const uint64_t aHi = a >> 32;
  const uint64_t bLo = static_cast<uint32_t>(b);
  const uint64_t bHi = b >> 32;
  const uint64_t t0 = aLo * bLo;
  const uint64_t t1 = aHi * bLo + (t0 >> 32);
  const uint64_t t2 = aLo * bHi + static_cast<uint32_t>(t1);
  const uint64_t lo = (t2 << 32) | static_cast<uint32_t>(t0);
  const uint64_t hi = aHi * bHi + (t1 >> 32) + (t2 >> 32);
  if (hi >= divisor) return std::nullopt;

  uint64_t remainder = hi;
  uint64_t quotient = 0;
  for (int bit = 63; bit >= 0; --bit) {
    remainder = (remainder << 1) | ((lo >> bit) & 1u);
    quotient <<= 1;
    if (remainder >= divisor) {
      remainder -= divisor;
      quotient |= 1u;
    }
  }
  if (remainder != 0) {
    if (quotient == kMax) return std::nullopt;
    ++quotient;
  }
  return quotient;
}

std::expected<uint64_t, std::string> parseTime(std::string_view text, uint64_t perSecond, uint64_t sampleRateMilliHz) {
  bool anyDigit = false;
  uint64_t count = 0;
  bool space = false;
  for (const char c : text) {
    if (space && ((c >= '0' && c <= '9') || c == '\'')) return std::unexpected(std::string(kSpaceInside));
    if (c >= '0' && c <= '9') {
      const uint64_t digit = static_cast<uint64_t>(c - '0');
      if (count > (kMax - digit) / 10) return tooBig();
      count = count * 10 + digit;
      anyDigit = true;
    } else if (c == '.') {
      return std::unexpected(
        std::string("length in time must be whole number; for less use smaller unit, such as 500 ms"));
    } else if (c == '\'') {
      continue;
    } else if (c == ',') {
      return std::unexpected(std::string(kNoComma));
    } else if (isSpace(c)) {
      space = true;
    } else {
      return std::unexpected(std::format("length has '{}' before its unit", c));
    }
  }
  if (!anyDigit) return std::unexpected(std::string("length has no digits"));
  if (count == 0) return std::unexpected(std::string("length is zero"));
  if (sampleRateMilliHz == 0) return std::unexpected(std::string("length in time needs sample rate"));

  const std::optional<uint64_t> samples = mulDivCeil(count, sampleRateMilliHz, 1'000 * perSecond);
  if (!samples) return tooBig();
  return *samples;
}

}

std::expected<uint64_t, std::string> parseLength(std::string_view text, uint64_t sampleRateMilliHz) {
  while (!text.empty() && isSpace(text.front())) text.remove_prefix(1);
  while (!text.empty() && isSpace(text.back())) text.remove_suffix(1);
  if (const std::optional<uint64_t> perSecond = stripTimeUnit(text)) {
    return parseTime(text, *perSecond, sampleRateMilliHz);
  }
  return parseSamples(text);
}

}
