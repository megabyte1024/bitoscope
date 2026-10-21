/*
 * SPDX-License-Identifier: MIT
 */

#include "log_view.h"

#include <utility>

namespace bitoscope::log {

LogView::LogView(std::size_t capacity) : capacity_(capacity == 0 ? 1 : capacity) {}

std::size_t LogView::update(const LogRing& ring) {
  RingSnapshot snapshot = ring.snapshotSince(lastSeq_);
  lastSeq_ = snapshot.lastSeq;
  missed_ += snapshot.missed;
  const std::size_t taken = snapshot.records.size();
  for (LogRecord& record : snapshot.records) {
    records_.push_back(std::move(record));
  }
  while (records_.size() > capacity_) {
    records_.pop_front();
  }
  return taken;
}

bool LogView::shown(const LogRecord& record, LogLevel minimum) {
  const LogLevel level = record.level == LogLevel::unspecified ? LogLevel::info : record.level;
  return static_cast<int>(level) >= static_cast<int>(minimum);
}

std::size_t LogView::shownCount(LogLevel minimum) const {
  std::size_t count = 0;
  for (const LogRecord& record : records_) {
    count += shown(record, minimum) ? 1 : 0;
  }
  return count;
}

}
