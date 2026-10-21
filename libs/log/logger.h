/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <format>
#include <memory>
#include <string_view>
#include <utility>

#include "log_file_sink.h"
#include "log_record.h"
#include "log_ring.h"

namespace bitoscope::log {

class Logger {
 public:
  explicit Logger(size_t ringCapacity, std::unique_ptr<LogFileSink> fileSink = nullptr);

  void log(LogLevel level, std::string_view source, std::string_view message);

  template <typename... Args>
  void logf(LogLevel level, std::string_view source, std::format_string<Args...> message,
            Args&&... args) {
    log(level, source, std::format(message, std::forward<Args>(args)...));
  }

  const LogRing& ring() const { return ring_; }
  LogRing& ring() { return ring_; }

  LogFileSink* fileSink() { return fileSink_.get(); }

 private:
  LogRing ring_;
  std::unique_ptr<LogFileSink> fileSink_;
};

void setGlobalLogger(Logger* logger);
Logger* globalLogger();

}
