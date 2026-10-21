/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

#include "log_record.h"

namespace bitoscope::log {

struct RingSnapshot {
  std::vector<LogRecord> records;
  uint64_t missed = 0;
  uint64_t lastSeq = 0;
};

class LogRing {
 public:
  explicit LogRing(size_t capacity);

  void push(LogRecord record);

  RingSnapshot snapshotSince(uint64_t sinceSeq) const;

  uint64_t droppedTotal() const;

  size_t size() const;

  size_t capacity() const { return capacity_; }

 private:
  size_t capacity_;
  mutable std::mutex mutex_;
  std::vector<LogRecord> storage_;
  uint64_t nextSeq_ = 0;
};

}
