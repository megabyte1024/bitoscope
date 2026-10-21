/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <chrono>
#include <cstdint>
#include <string>

#include "log_level.h"

namespace bitoscope::log {

struct LogRecord {
  std::chrono::system_clock::time_point timestamp{};
  LogLevel level = LogLevel::unspecified;
  std::string source;
  std::string message;

  bool operator==(const LogRecord&) const = default;
};

}
