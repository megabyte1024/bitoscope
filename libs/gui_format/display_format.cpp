/*
 * SPDX-License-Identifier: MIT
 */

#include "display_format.h"

#include <algorithm>
#include <charconv>
#include <format>
#include <limits>
#include <string>

namespace bitoscope::gui_format {

std::string withThousands(std::uint64_t value) {
  const std::string digits = std::to_string(value);
  std::string out;
  for (std::size_t i = 0; i < digits.size(); ++i) {
    if (i != 0 && (digits.size() - i) % 3 == 0) {
      out += '\'';
    }
    out += digits[i];
  }
  return out;
}

std::string formatFixed(std::uint64_t value, unsigned decimals) {
  // WHY: Format from decimal text, not by dividing by 10^decimals: that overflows past 19 decimals.
  std::string digits = std::to_string(value);
  if (digits.size() <= decimals) {
    digits.insert(0, decimals + 1 - digits.size(), '0');
  }
  const std::size_t point = digits.size() - decimals;
  std::string fraction = digits.substr(point);
  while (!fraction.empty() && fraction.back() == '0') {
    fraction.pop_back();
  }
  std::uint64_t whole = 0;
  std::from_chars(digits.data(), digits.data() + point, whole);
  std::string out = withThousands(whole);
  if (!fraction.empty()) {
    out += '.';
    out += fraction;
  }
  return out;
}

std::string formatFrequency(std::uint64_t milliHz) {
  struct Unit {
    std::uint64_t milliHz;
    unsigned decimals;
    const char* name;
  };
  static constexpr Unit kUnits[] = {
    {1'000'000'000'000, 12, "GHz"}, {1'000'000'000, 9, "MHz"}, {1'000'000, 6, "kHz"}, {1'000, 3, "Hz"}};
  if (milliHz == 0) {
    return "0 Hz";
  }
  for (const Unit& unit : kUnits) {
    if (milliHz >= unit.milliHz) {
      return std::format("{} {}", formatFixed(milliHz, unit.decimals), unit.name);
    }
  }
  return std::format("{} mHz", milliHz);
}

std::string formatFrequencyRounded(std::uint64_t milliHz, unsigned maxDecimals) {
  struct Unit {
    std::uint64_t milliHz;
    unsigned decimals;
    const char* name;
  };
  static constexpr Unit kUnits[] = {{1'000'000'000'000, 12, "GHz"},
                                    {1'000'000'000, 9, "MHz"},
                                    {1'000'000, 6, "kHz"},
                                    {1'000, 3, "Hz"},
                                    {1, 0, "mHz"}};
  if (milliHz == 0) {
    return "0 Hz";
  }
  std::size_t u = 0;
  while (milliHz < kUnits[u].milliHz) {
    ++u;
  }
  for (;;) {
    const unsigned decimals = (std::min)(maxDecimals, kUnits[u].decimals);
    std::uint64_t step = kUnits[u].milliHz;
    for (unsigned i = 0; i < decimals; ++i) {
      step /= 10;
    }
    std::uint64_t q = milliHz / step;
    if ((milliHz % step) * 2 >= step) {
      ++q;
    }
    std::uint64_t thousand = 1000;
    for (unsigned i = 0; i < decimals; ++i) {
      thousand *= 10;
    }
    if (q >= thousand && u > 0) {
      --u;
      continue;
    }
    return std::format("{} {}", formatFixed(q, decimals), kUnits[u].name);
  }
}

std::string formatPercent(std::uint32_t basisPoints) {
  return formatFixed(basisPoints, 2) + " %";
}

std::string formatWholePercent(std::uint64_t done, std::uint64_t total) {
  if (total == 0) {
    return "0 %";
  }
  if (done >= total) {
    return "100 %";
  }
  // WHY: Shift both down until done * 100 cannot overflow; the result moves by a hair at most.
  while (total > std::numeric_limits<std::uint64_t>::max() / 100) {
    done >>= 1;
    total >>= 1;
  }
  const std::uint64_t percent = total == 0 ? 0 : done * 100 / total;
  return std::to_string((std::min)(percent, std::uint64_t{99})) + " %";
}

std::string formatTimePs(std::int64_t picoseconds) {
  struct Unit {
    std::uint64_t ps;
    unsigned decimals;
    const char* name;
  };
  static constexpr Unit kUnits[] = {
    {1'000'000'000'000, 12, "s"}, {1'000'000'000, 9, "ms"}, {1'000'000, 6, "us"}, {1'000, 3, "ns"}};
  const bool negative = picoseconds < 0;
  // WHY: Negate in uint64_t arithmetic: negating INT64_MIN as signed is undefined behaviour.
  const std::uint64_t magnitude =
      negative ? (0 - static_cast<std::uint64_t>(picoseconds)) : static_cast<std::uint64_t>(picoseconds);

  std::string body;
  if (magnitude == 0) {
    body = "0 ps";
  } else {
    body.clear();
    for (const Unit& unit : kUnits) {
      if (magnitude >= unit.ps) {
        body = formatFixed(magnitude, unit.decimals) + " " + unit.name;
        break;
      }
    }
    if (body.empty()) {
      body = std::to_string(magnitude) + " ps";
    }
  }
  return negative ? "-" + body : body;
}

std::string formatIpv4(std::uint32_t address) {
  return std::format("{}.{}.{}.{}", (address >> 24) & 0xFF, (address >> 16) & 0xFF,
                     (address >> 8) & 0xFF, address & 0xFF);
}

std::expected<std::uint32_t, std::string> parseIpv4(std::string_view text) {
  std::uint32_t address = 0;
  std::size_t pos = 0;
  for (int part = 0; part < 4; ++part) {
    if (part != 0) {
      if (pos >= text.size() || text[pos] != '.') {
        return std::unexpected(std::string("IPv4 address has four numbers separated by '.'"));
      }
      ++pos;
    }
    const std::size_t start = pos;
    while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9' && pos - start < 4) {
      ++pos;
    }
    const std::size_t length = pos - start;
    if (length == 0) {
      return std::unexpected(std::format("number {} is missing or not decimal", part + 1));
    }
    if (length > 3) {
      return std::unexpected(std::format("number {} is larger than 255", part + 1));
    }
    if (length > 1 && text[start] == '0') {
      return std::unexpected(std::format("number {} has leading zero", part + 1));
    }
    unsigned value = 0;
    std::from_chars(text.data() + start, text.data() + pos, value);
    if (value > 255) {
      return std::unexpected(std::format("number {} is larger than 255", part + 1));
    }
    address = (address << 8) | value;
  }
  if (pos != text.size()) {
    return std::unexpected(std::string("IPv4 address has four numbers separated by '.'"));
  }
  return address;
}

}
