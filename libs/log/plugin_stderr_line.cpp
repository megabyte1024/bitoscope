/*
 * SPDX-License-Identifier: MIT
 */

#include "plugin_stderr_line.h"

#include <array>
#include <utility>

namespace bitoscope::log {

PluginStderrLine parsePluginStderrLine(std::string_view line) {
  static constexpr std::array<std::pair<std::string_view, LogLevel>, 4> kTags{{
      {": [debug] ", LogLevel::debug},
      {": [info] ", LogLevel::info},
      {": [warning] ", LogLevel::warning},
      {": [error] ", LogLevel::error},
  }};
  size_t earliest = std::string_view::npos;
  LogLevel level = LogLevel::debug;
  size_t tagSize = 0;
  for (const auto& [tag, tagLevel] : kTags) {
    const size_t at = line.find(tag);
    if (at != std::string_view::npos && at != 0 && at < earliest) {
      earliest = at;
      level = tagLevel;
      tagSize = tag.size();
    }
  }
  if (earliest == std::string_view::npos) return PluginStderrLine{LogLevel::debug, std::string(line)};
  std::string message = "[";
  message += line.substr(0, earliest);
  message += "] ";
  message += line.substr(earliest + tagSize);
  return PluginStderrLine{level, std::move(message)};
}

}
