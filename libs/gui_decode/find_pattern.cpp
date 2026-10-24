/*
 * SPDX-License-Identifier: MIT
 */

#include "find_pattern.h"

#include <array>

namespace bitoscope::gui_decode {

namespace {

constexpr unsigned int firstPrintable = 0x20;
constexpr unsigned int lastPrintable = 0x7E;
constexpr int notAHexDigit = -1;

int hexDigitValue(unsigned int c) {
  if (c >= '0' && c <= '9') {
    return static_cast<int>(c - '0');
  }
  if (c >= 'a' && c <= 'f') {
    return static_cast<int>(c - 'a') + 10;
  }
  if (c >= 'A' && c <= 'F') {
    return static_cast<int>(c - 'A') + 10;
  }
  return notAHexDigit;
}

bool isPrintableAscii(unsigned int c) { return c >= firstPrintable && c <= lastPrintable; }

}

std::expected<std::vector<std::uint8_t>, FindPatternError> parseFindPattern(std::string_view text, FindMode mode) {
  std::vector<std::uint8_t> bytes;
  if (mode == FindMode::Ascii) {
    bytes.reserve(text.size());
    for (const char ch : text) {
      const auto c = static_cast<unsigned int>(static_cast<unsigned char>(ch));
      if (!isPrintableAscii(c)) {
        return std::unexpected(FindPatternError::NotAscii);
      }
      bytes.push_back(static_cast<std::uint8_t>(c));
    }
  } else {
    bool highHalfPending = false;
    int high = 0;
    for (const char ch : text) {
      const auto c = static_cast<unsigned int>(static_cast<unsigned char>(ch));
      if (c == ' ') {
        continue;
      }
      const int digit = hexDigitValue(c);
      if (digit == notAHexDigit) {
        return std::unexpected(FindPatternError::NotHex);
      }
      if (highHalfPending) {
        bytes.push_back(static_cast<std::uint8_t>((high << 4) | digit));
        highHalfPending = false;
      } else {
        high = digit;
        highHalfPending = true;
      }
    }
    if (highHalfPending) {
      return std::unexpected(FindPatternError::OddHexDigits);
    }
  }
  if (bytes.empty()) {
    return std::unexpected(FindPatternError::Empty);
  }
  return bytes;
}

bool acceptsFindChar(FindMode mode, unsigned int c) {
  if (mode == FindMode::Ascii) {
    return isPrintableAscii(c);
  }
  return c == ' ' || hexDigitValue(c) != notAHexDigit;
}

std::optional<std::string> convertFindText(std::string_view text, FindMode from, FindMode to) {
  const auto parsed = parseFindPattern(text, from);
  if (!parsed) {
    return std::nullopt;
  }
  if (from == to) {
    return std::string(text);
  }
  std::string converted;
  if (to == FindMode::Hex) {
    constexpr std::array<char, 16> digits{'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'};
    converted.reserve(parsed->size() * 3);
    for (const std::uint8_t byte : *parsed) {
      if (!converted.empty()) {
        converted.push_back(' ');
      }
      converted.push_back(digits[byte >> 4]);
      converted.push_back(digits[byte & 15]);
    }
    return converted;
  }
  converted.reserve(parsed->size());
  for (const std::uint8_t byte : *parsed) {
    if (!isPrintableAscii(byte)) {
      return std::nullopt;
    }
    converted.push_back(static_cast<char>(byte));
  }
  return converted;
}

}
