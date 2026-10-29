/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <expected>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

#include "block_clearing.h"
#include "capture_allocator.h"

namespace bitoscope::capture {

class BlockSupplyAhead final : public ArenaBlockSupply {
 public:
  BlockSupplyAhead(ArenaBlockSupply& inner, std::chrono::milliseconds wait_limit, ClearingThreads* threads = nullptr);
  ~BlockSupplyAhead() override;

  BlockSupplyAhead(const BlockSupplyAhead&) = delete;
  BlockSupplyAhead& operator=(const BlockSupplyAhead&) = delete;

  std::expected<std::shared_ptr<uint8_t[]>, BlockRefusal> supply(uint64_t index, uint64_t bytes) override;

  void prepare(uint64_t index, uint64_t bytes) override;

  void abandon();

  void close();

 private:
  using Taken = std::expected<std::shared_ptr<uint8_t[]>, BlockRefusal>;

  void joinHelper();

  ArenaBlockSupply* inner_ = nullptr;
  std::chrono::milliseconds wait_limit_;
  ClearingThreads* threads_ = nullptr;
  SystemClearingThreads system_threads_;
  std::mutex mutex_;
  std::condition_variable taken_cv_;
  std::optional<uint64_t> ahead_index_;
  std::optional<Taken> ahead_;
  bool abandoned_ = false;
  std::thread helper_;
};

}
