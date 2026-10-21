/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>

#include "log_level.h"
#include "log_record.h"
#include "log_ring.h"

namespace bitoscope::log {

class LogView {
 public:
  explicit LogView(std::size_t capacity);

  std::size_t update(const LogRing& ring);

  const std::deque<LogRecord>& records() const { return records_; }

  std::uint64_t missed() const { return missed_; }

  static bool shown(const LogRecord& record, LogLevel minimum);
  std::size_t shownCount(LogLevel minimum) const;

 private:
  std::size_t capacity_;
  std::deque<LogRecord> records_;
  std::uint64_t lastSeq_ = 0;
  std::uint64_t missed_ = 0;
};

}
