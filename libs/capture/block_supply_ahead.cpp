/*
 * SPDX-License-Identifier: MIT
 */

#include "block_supply_ahead.h"

#include <exception>
#include <format>
#include <utility>

namespace bitoscope::capture {

BlockSupplyAhead::BlockSupplyAhead(ArenaBlockSupply& inner, std::chrono::milliseconds wait_limit,
                                   ClearingThreads* threads)
    : inner_(&inner), wait_limit_(wait_limit), threads_(threads != nullptr ? threads : &system_threads_) {}

BlockSupplyAhead::~BlockSupplyAhead() { close(); }

std::expected<std::shared_ptr<uint8_t[]>, BlockRefusal> BlockSupplyAhead::supply(uint64_t index, uint64_t bytes) {
  const auto refused = [index] {
    return std::unexpected(
        BlockRefusal{BlockRefusal::Kind::Refused, std::format("capture stopped before memory block {} was taken", index)});
  };
  std::unique_lock lock(mutex_);
  if (abandoned_) return refused();
  if (ahead_index_) {
    const bool done = taken_cv_.wait_for(lock, wait_limit_, [this] { return ahead_.has_value() || abandoned_; });
    if (!done) {
      return std::unexpected(BlockRefusal{
          BlockRefusal::Kind::Late, std::format("memory block {} of capture was not ready within {} ms", *ahead_index_,
                                                wait_limit_.count())});
    }
    if (!ahead_) return refused();
    Taken taken = std::move(*ahead_);
    const uint64_t taken_index = *ahead_index_;
    ahead_.reset();
    ahead_index_.reset();
    lock.unlock();
    joinHelper();
    if (taken_index == index) return taken;
  } else {
    lock.unlock();
  }
  return inner_->supply(index, bytes);
}

void BlockSupplyAhead::prepare(uint64_t index, uint64_t bytes) {
  std::lock_guard lock(mutex_);
  if (abandoned_ || ahead_index_) return;
  ahead_index_ = index;
  try {
    helper_ = threads_->start([this, index, bytes] {
      Taken taken = std::unexpected(BlockRefusal{});
      try {
        taken = inner_->supply(index, bytes);
      } catch (const std::exception& e) {
        taken = std::unexpected(BlockRefusal{BlockRefusal::Kind::Refused, e.what()});
      }
      {
        std::lock_guard done(mutex_);
        ahead_ = std::move(taken);
      }
      taken_cv_.notify_all();
    });
  } catch (const std::exception&) {
    ahead_index_.reset();
  }
}

void BlockSupplyAhead::abandon() {
  {
    std::lock_guard lock(mutex_);
    abandoned_ = true;
  }
  taken_cv_.notify_all();
}

void BlockSupplyAhead::close() {
  abandon();
  joinHelper();
  std::lock_guard lock(mutex_);
  ahead_.reset();
  ahead_index_.reset();
}

void BlockSupplyAhead::joinHelper() {
  if (helper_.joinable()) helper_.join();
}

}
