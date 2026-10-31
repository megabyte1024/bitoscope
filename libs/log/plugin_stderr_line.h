/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <string>
#include <string_view>

#include "log_level.h"

namespace bitoscope::log {

struct PluginStderrLine {
  LogLevel level = LogLevel::debug;
  std::string message;
};

PluginStderrLine parsePluginStderrLine(std::string_view line);

}
