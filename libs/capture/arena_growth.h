/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <atomic>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <string>

#include "block_clearing.h"
#include "capture_allocator.h"
#include "capture_plan.h"
#include "memory_budget.h"
#include "memory_probe.h"

namespace bitoscope::capture {

class GrowingArenaMemory {
 public:
  GrowingArenaMemory(const MemoryBudget& budget, uint64_t arena_bytes, bool held_back_taken_before_arena);

  std::optional<std::string> refusal(MemoryProbe& probe, uint64_t index, uint64_t bytes) const;
  void took(uint64_t bytes);
  uint64_t takenBytes() const;
  uint64_t stillNeededBytes(uint64_t free_now) const;

 private:
  MemoryBudget budget_;
  uint64_t arena_bytes_ = 0;
  bool held_back_taken_before_arena_ = false;
  std::atomic<uint64_t> taken_{0};
};

class ArenaGrowth final : public ArenaBlockSupply {
 public:
  ArenaGrowth(MemoryProbe& probe, BlockSource& source, const AllocatorSettings& settings, const MemoryBudget& budget,
              uint64_t arena_bytes);

  std::expected<std::shared_ptr<uint8_t[]>, BlockRefusal> supply(uint64_t index, uint64_t bytes) override;

  uint64_t bytesTaken() const { return memory_->takenBytes(); }
  std::shared_ptr<const GrowingArenaMemory> memory() const { return memory_; }

 private:
  MemoryProbe* probe_ = nullptr;
  BlockSource* source_ = nullptr;
  AllocationPolicy policy_ = AllocationPolicy::Chunked;
  uint32_t reread_every_blocks_ = 1;
  uint32_t clearing_workers_ = 1;
  ClearingThreads* clearing_threads_ = nullptr;
  bool commit_without_clearing_ = false;
  SystemClearingThreads system_threads_;
  std::shared_ptr<GrowingArenaMemory> memory_;
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
