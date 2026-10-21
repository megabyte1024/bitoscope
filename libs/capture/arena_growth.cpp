/*
 * SPDX-License-Identifier: MIT
 */

#include "arena_growth.h"

#include <algorithm>
#include <format>
#include <new>
#include <span>
#include <utility>

namespace bitoscope::capture {

ArenaGrowth::ArenaGrowth(MemoryProbe& probe, BlockSource& source, const AllocatorSettings& settings,
                         const MemoryBudget& budget)
    : probe_(&probe),
      source_(&source),
      policy_(settings.policy),
      reread_every_blocks_((std::max)(settings.reread_every_blocks, uint32_t{1})),
      clearing_workers_(settings.clearing_workers),
      clearing_threads_(settings.clearing_threads),
      budget_(budget) {}

std::expected<std::shared_ptr<uint8_t[]>, std::string> ArenaGrowth::supply(uint64_t index, uint64_t bytes) {
  if (policy_ == AllocationPolicy::Chunked && index != 0 && index % reread_every_blocks_ == 0) {
    if (auto refused = freeRamRefusal(*probe_, budget_, index, bytes)) return std::unexpected(std::move(*refused));
  }
  try {
    auto block = source_->allocate(bytes);
    if (!block) return std::unexpected(std::format("allocation failed at block {}: {}", index, block.error()));
    const std::span<uint8_t> taken(block->get(), static_cast<size_t>(bytes));
    ClearingThreads& threads = clearing_threads_ != nullptr ? *clearing_threads_ : system_threads_;
    clearBlocks(std::span<const std::span<uint8_t>>(&taken, 1), clearing_workers_, threads);
    bytes_taken_ += bytes;
    return std::move(*block);
  } catch (const std::bad_alloc&) {
    return std::unexpected(std::format("allocation failed at block {}: std::bad_alloc", index));
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
  return GrowingArena{std::move(reservation), ArenaGrowth(probe, source, settings, shape->budget)};
}

}
