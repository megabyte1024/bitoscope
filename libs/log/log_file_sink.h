/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>

#include "log_record.h"

namespace bitoscope::log {

class LogFileSink {
 public:
  LogFileSink(std::filesystem::path path, uint64_t capBytes);

  bool write(const LogRecord& record);

  uint64_t failedWriteCount() const;

  uint64_t currentSize() const;

 private:
  bool rolloverIfNeeded(uint64_t nextLineSize);
  bool reopen();

  std::filesystem::path path_;
  uint64_t capBytes_;
  mutable std::mutex mutex_;
  std::ofstream stream_;
  uint64_t currentSize_ = 0;
  uint64_t failedWriteCount_ = 0;
  bool open_ = false;
};

}
