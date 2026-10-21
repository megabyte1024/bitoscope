/*
 * SPDX-License-Identifier: MIT
 */

#include "logger.h"

#include <atomic>

namespace bitoscope::log {

Logger::Logger(size_t ringCapacity, std::unique_ptr<LogFileSink> fileSink)
    : ring_(ringCapacity), fileSink_(std::move(fileSink)) {}

void Logger::log(LogLevel level, std::string_view source, std::string_view message) {
  LogRecord record;
  record.timestamp = std::chrono::system_clock::now();
  record.level = level;
  record.source = std::string(source);
  record.message = std::string(message);

  if (fileSink_) {
    fileSink_->write(record);
  }
  ring_.push(std::move(record));
}

namespace {
std::atomic<Logger*> g_globalLogger{nullptr};
}

void setGlobalLogger(Logger* logger) {
  g_globalLogger.store(logger, std::memory_order_release);
}

Logger* globalLogger() {
  return g_globalLogger.load(std::memory_order_acquire);
}

}
