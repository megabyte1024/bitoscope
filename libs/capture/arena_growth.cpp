/*
 * SPDX-License-Identifier: MIT
 */

#include "arena_growth.h"

#include <algorithm>
#include <format>
#include <new>
#include <optional>
#include <span>
#include <utility>

namespace bitoscope::capture {

GrowingArenaMemory::GrowingArenaMemory(const MemoryBudget& budget, uint64_t arena_bytes,
                                       bool held_back_taken_before_arena)
    : budget_(budget), arena_bytes_(arena_bytes), held_back_taken_before_arena_(held_back_taken_before_arena) {}

std::optional<std::string> GrowingArenaMemory::refusal(MemoryProbe& probe, uint64_t index, uint64_t bytes) const {
  const std::optional<uint64_t> arena_taken =
      held_back_taken_before_arena_ ? std::optional<uint64_t>(takenBytes()) : std::nullopt;
  return freeRamRefusal(probe, budget_, index, bytes, arena_taken);
}

void GrowingArenaMemory::took(uint64_t bytes) { taken_.fetch_add(bytes, std::memory_order_relaxed); }

uint64_t GrowingArenaMemory::takenBytes() const { return taken_.load(std::memory_order_relaxed); }

uint64_t GrowingArenaMemory::stillNeededBytes(uint64_t free_now) const {
  const uint64_t taken = takenBytes();
  const uint64_t withheld =
      withheldBytes(held_back_taken_before_arena_ ? withHeldBackStillToCome(budget_, free_now, taken) : budget_);
  const uint64_t untaken = arena_bytes_ > taken ? arena_bytes_ - taken : 0;
  return withheld + untaken;
}

ArenaGrowth::ArenaGrowth(MemoryProbe& probe, BlockSource& source, const AllocatorSettings& settings,
                         const MemoryBudget& budget, uint64_t arena_bytes)
    : probe_(&probe),
      source_(&source),
      policy_(settings.policy),
      reread_every_blocks_((std::max)(settings.reread_every_blocks, uint32_t{1})),
      clearing_workers_(settings.clearing_workers),
      clearing_threads_(settings.clearing_threads),
      commit_without_clearing_(settings.commit_without_clearing),
      memory_(std::make_shared<GrowingArenaMemory>(budget, arena_bytes, settings.held_back_taken_before_arena)) {}

std::expected<std::shared_ptr<uint8_t[]>, BlockRefusal> ArenaGrowth::supply(uint64_t index, uint64_t bytes) {
  const auto refuse = [](std::string why) {
    return std::unexpected(BlockRefusal{BlockRefusal::Kind::Refused, std::move(why)});
  };
  if (policy_ == AllocationPolicy::Chunked && index != 0 && index % reread_every_blocks_ == 0) {
    if (auto refused = memory_->refusal(*probe_, index, bytes)) return refuse(std::move(*refused));
  }
  try {
    auto block = source_->allocate(bytes);
    if (!block) return refuse(std::format("allocation failed at block {}: {}", index, block.error()));
    const std::span<uint8_t> taken(block->get(), static_cast<size_t>(bytes));
    ClearingThreads& threads = clearing_threads_ != nullptr ? *clearing_threads_ : system_threads_;
    const std::span<const std::span<uint8_t>> blocks(&taken, 1);
    if (commit_without_clearing_) {
      commitBlocks(blocks, clearing_workers_, threads);
    } else {
      clearBlocks(blocks, clearing_workers_, threads);
    }
    memory_->took(bytes);
    return std::move(*block);
  } catch (const std::bad_alloc&) {
    return refuse(std::format("allocation failed at block {}: std::bad_alloc", index));
  }
}

std::expected<GrowingArena, std::string> planGrowingArena(const CapturePlan& plan, MemoryProbe& probe,
                                                          BlockSource& source, const AllocatorSettings& settings) {
  auto budget = computeBudget(probe, settings.budget);
  if (!budget) return std::unexpected(budget.error());
  return planGrowingArena(plan, *budget, probe, source, settings);
}

std::expected<GrowingArena, std::string> planGrowingArena(const CapturePlan& plan, const MemoryBudget& budget,
                                                          MemoryProbe& probe, BlockSource& source,
                                                          const AllocatorSettings& settings) {
  auto shape = shapeArena(plan, budget, settings);
  if (!shape) return std::unexpected(shape.error());
  Reservation reservation;
  reservation.plan = plan;
  reservation.slotsRequested = plan.slots();
  reservation.block_bytes = shape->block_bytes;
  reservation.arena_bytes = shape->arena_target;
  try {
    reservation.blocks.resize(static_cast<size_t>(shape->block_count));
  } catch (const std::bad_alloc&) {
    return std::unexpected("out of memory tracking capture's arena");
  }
  reservation.samplesFloor = (std::min)(guaranteedSamples(reservation), plan.totalSamples());
  if (shape->cut) reservation.warnings.push_back(*shape->cut);
  return GrowingArena{std::move(reservation),
                      ArenaGrowth(probe, source, settings, shape->budget, shape->arena_target)};
}

}
