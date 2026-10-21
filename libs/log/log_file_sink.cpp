/*
 * SPDX-License-Identifier: MIT
 */

#include "log_file_sink.h"

#include <chrono>
#include <format>
#include <system_error>

#include "text_escape.h"

namespace bitoscope::log {

namespace {

std::string_view levelName(LogLevel level) {
  switch (level) {
    case LogLevel::debug:
      return "debug";
    case LogLevel::info:
      return "info";
    case LogLevel::warning:
      return "warning";
    case LogLevel::error:
      return "error";
    case LogLevel::unspecified:
    default:
      return "unspecified";
  }
}

std::string formatLine(const LogRecord& record) {
  int64_t ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                   record.timestamp.time_since_epoch())
                   .count();
  return std::format("{} {} {}: {}\n", ms, levelName(record.level), record.source,
                      escapeControlChars(record.message));
}

}

LogFileSink::LogFileSink(std::filesystem::path path, uint64_t capBytes)
    : path_(std::move(path)), capBytes_(capBytes == 0 ? 1 : capBytes) {
  reopen();
}

bool LogFileSink::reopen() {
  stream_.close();
  stream_.clear();
  stream_.open(path_, std::ios::binary | std::ios::app);
  open_ = stream_.is_open();
  if (open_) {
    std::error_code ec;
    auto size = std::filesystem::file_size(path_, ec);
    currentSize_ = ec ? 0 : static_cast<uint64_t>(size);
  } else {
    currentSize_ = 0;
  }
  return open_;
}

bool LogFileSink::rolloverIfNeeded(uint64_t nextLineSize) {
  if (!open_) {
    return false;
  }
  if (currentSize_ == 0 || currentSize_ + nextLineSize <= capBytes_) {
    return true;
  }

  stream_.close();
  std::error_code ec;
  std::filesystem::path rolled = path_;
  rolled += ".1";
  std::filesystem::remove(rolled, ec);
  ec.clear();
  std::filesystem::rename(path_, rolled, ec);
  if (ec) {
    // WHY: A failed rollover keeps writing to the existing file instead of losing the sink;
    // the write still counts as failed.
    reopen();
    return false;
  }

  stream_.clear();
  stream_.open(path_, std::ios::binary | std::ios::trunc);
  open_ = stream_.is_open();
  currentSize_ = 0;
  return open_;
}

bool LogFileSink::write(const LogRecord& record) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!open_) {
    ++failedWriteCount_;
    return false;
  }

  std::string line = formatLine(record);
  if (!rolloverIfNeeded(line.size())) {
    ++failedWriteCount_;
    return false;
  }

  stream_.write(line.data(), static_cast<std::streamsize>(line.size()));
  stream_.flush();
  if (!stream_.good()) {
    open_ = false;
    ++failedWriteCount_;
    return false;
  }

  currentSize_ += line.size();
  return true;
}

uint64_t LogFileSink::failedWriteCount() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return failedWriteCount_;
}

uint64_t LogFileSink::currentSize() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return currentSize_;
}

}
