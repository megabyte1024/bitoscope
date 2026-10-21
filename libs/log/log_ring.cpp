/*
 * SPDX-License-Identifier: MIT
 */

#include "log_ring.h"

#include <algorithm>

namespace bitoscope::log {

LogRing::LogRing(size_t capacity) : capacity_(capacity == 0 ? 1 : capacity) {
  storage_.resize(capacity_);
}

void LogRing::push(LogRecord record) {
  std::lock_guard<std::mutex> lock(mutex_);
  uint64_t seq = ++nextSeq_;
  storage_[(seq - 1) % capacity_] = std::move(record);
}

RingSnapshot LogRing::snapshotSince(uint64_t sinceSeq) const {
  std::lock_guard<std::mutex> lock(mutex_);

  RingSnapshot snapshot;
  snapshot.lastSeq = nextSeq_;

  uint64_t oldestHeldSeq = (nextSeq_ > capacity_) ? (nextSeq_ - capacity_ + 1) : (nextSeq_ > 0 ? 1 : 0);

  if (oldestHeldSeq > 0 && sinceSeq < oldestHeldSeq - 1) {
    snapshot.missed = (oldestHeldSeq - 1) - sinceSeq;
  } else {
    snapshot.missed = 0;
  }

  uint64_t start = std::max(sinceSeq, oldestHeldSeq > 0 ? oldestHeldSeq - 1 : 0);
  for (uint64_t seq = start + 1; seq <= nextSeq_; ++seq) {
    snapshot.records.push_back(storage_[(seq - 1) % capacity_]);
  }
  return snapshot;
}

uint64_t LogRing::droppedTotal() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return (nextSeq_ > capacity_) ? (nextSeq_ - capacity_) : 0;
}

size_t LogRing::size() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return static_cast<size_t>(std::min<uint64_t>(nextSeq_, capacity_));
}

}
