/*
 * SPDX-License-Identifier: MIT
 */

#include "serial_ports.h"

#include <cstdint>
#include <string_view>

namespace bitoscope::gui_linux {

namespace {

std::optional<std::uint64_t> parseType(std::string_view text) {
  const auto isSpace = [](char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; };
  while (!text.empty() && isSpace(text.front())) {
    text.remove_prefix(1);
  }
  while (!text.empty() && isSpace(text.back())) {
    text.remove_suffix(1);
  }
  if (text.empty() || text.size() > 9) {
    return std::nullopt;
  }
  std::uint64_t value = 0;
  for (const char c : text) {
    if (c < '0' || c > '9') {
      return std::nullopt;
    }
    value = value * 10 + static_cast<std::uint64_t>(c - '0');
  }
  return value;
}

bool isPort(const TtyEntry& entry) {
  if (entry.name.empty() || entry.name.find('/') != std::string::npos) {
    return false;
  }
  if (!entry.hasDevice || !entry.deviceNodeExists) {
    return false;
  }
  // WHY: Type 0 is PORT_UNKNOWN: a serial-core placeholder with no UART behind it.
  return !(entry.uartType && parseType(*entry.uartType) == std::uint64_t{0});
}

}

std::vector<std::string> serialPortsFromSysfs(std::span<const TtyEntry> entries) {
  std::vector<std::string> ports;
  for (const TtyEntry& entry : entries) {
    if (isPort(entry)) {
      ports.push_back("/dev/" + entry.name);
    }
  }
  return ports;
}

}
