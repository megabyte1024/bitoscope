/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <expected>
#include <memory>
#include <string>

#include "block_clearing.h"
#include "capture_allocator.h"
#include "capture_plan.h"
#include "memory_budget.h"
#include "memory_probe.h"

namespace bitoscope::capture {

class ArenaGrowth final : public ArenaBlockSupply {
 public:
  ArenaGrowth(MemoryProbe& probe, BlockSource& source, const AllocatorSettings& settings, const MemoryBudget& budget);

  std::expected<std::shared_ptr<uint8_t[]>, std::string> supply(uint64_t index, uint64_t bytes) override;

  uint64_t bytesTaken() const { return bytes_taken_; }

 private:
  MemoryProbe* probe_ = nullptr;
  BlockSource* source_ = nullptr;
  AllocationPolicy policy_ = AllocationPolicy::Chunked;
  uint32_t reread_every_blocks_ = 1;
  uint32_t clearing_workers_ = 1;
  ClearingThreads* clearing_threads_ = nullptr;
  SystemClearingThreads system_threads_;
  MemoryBudget budget_;
  uint64_t bytes_taken_ = 0;
};

struct GrowingArena {
  Reservation reservation;
  ArenaGrowth growth;
};

std::expected<GrowingArena, std::string> planGrowingArena(const CapturePlan& plan, MemoryProbe& probe,
                                                          BlockSource& source, const AllocatorSettings& settings);

std::expected<GrowingArena, std::string> planGrowingArena(const CapturePlan& plan, const MemoryBudget& budget,
                                                          MemoryProbe& probe, BlockSource& source,
                                                          const AllocatorSettings& settings);

}
