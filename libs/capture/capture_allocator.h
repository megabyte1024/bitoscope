/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <algorithm>
#include <cstdint>
#include <expected>
#include <format>
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include "block_clearing.h"
#include "capture_plan.h"
#include "history_ring_floor.h"
#include "memory_budget.h"
#include "memory_probe.h"

namespace bitoscope::capture {

inline constexpr uint64_t kArenaBlockBytes = 64ULL << 20;

class BlockSource {
 public:
  virtual ~BlockSource() = default;
  virtual std::expected<std::shared_ptr<uint8_t[]>, std::string> allocate(uint64_t bytes) = 0;
};

class SystemBlockSource final : public BlockSource {
 public:
  std::expected<std::shared_ptr<uint8_t[]>, std::string> allocate(uint64_t bytes) override {
    if (bytes > std::numeric_limits<size_t>::max()) {
      return std::unexpected(std::format("{} bytes does not fit this platform's address space",
                                         bytes));
    }
    try {
      return std::make_shared_for_overwrite<uint8_t[]>(static_cast<size_t>(bytes));
    } catch (const std::bad_alloc&) {
      return std::unexpected(std::format("out of memory allocating {} bytes", bytes));
    } catch (const std::length_error&) {
      return std::unexpected(std::format("cannot allocate {} bytes", bytes));
    }
  }
};

struct BlockRefusal {
  enum class Kind : uint8_t { Refused, Late };
  Kind kind = Kind::Refused;
  std::string why;
};

class ArenaBlockSupply {
 public:
  virtual ~ArenaBlockSupply() = default;
  virtual std::expected<std::shared_ptr<uint8_t[]>, BlockRefusal> supply(uint64_t index, uint64_t bytes) = 0;
  virtual void prepare([[maybe_unused]] uint64_t index, [[maybe_unused]] uint64_t bytes) {}
};

enum class AllocationPolicy { Whole, Chunked };

enum class ArenaCap : uint8_t { Plan, WholeSlotsWithRingMargin };

struct AllocatorSettings {
  AllocationPolicy policy = AllocationPolicy::Chunked;
  BudgetSettings budget;
  uint32_t reread_every_blocks = 8;
  uint64_t arena_block_bytes = 0;
  ArenaCap arena_cap = ArenaCap::Plan;
  uint32_t clearing_workers = 1;
  ClearingThreads* clearing_threads = nullptr;
  bool held_back_taken_before_arena = false;
  bool commit_without_clearing = false;
};

class Reservation {
 public:
  Reservation() = default;
  Reservation(const Reservation&) = delete;
  Reservation& operator=(const Reservation&) = delete;
  Reservation(Reservation&&) = default;
  Reservation& operator=(Reservation&&) = default;

  uint64_t slotsRequested = 0;
  uint64_t samplesFloor = 0;
  std::vector<std::string> warnings;

  uint64_t arenaBytes() const { return arena_bytes; }
  uint64_t blockBytes() const { return block_bytes; }
  uint64_t blockCount() const { return blocks.size(); }

  uint64_t blockSize(uint64_t index) const {
    if (index >= blocks.size()) return 0;
    const uint64_t start = index * block_bytes;
    if (start >= arena_bytes) return 0;
    return (std::min)(block_bytes, arena_bytes - start);
  }

  std::span<uint8_t> block(uint64_t index) const {
    if (index >= blocks.size() || !blocks[index]) return {};
    return {blocks[index].get(), static_cast<size_t>(blockSize(index))};
  }

  std::span<uint8_t> at(uint64_t offset, uint64_t count) const {
    if (block_bytes == 0) return {};
    const uint64_t index = offset / block_bytes;
    const uint64_t within = offset % block_bytes;
    if (index >= blocks.size() || !blocks[index]) return {};
    const uint64_t size = blockSize(index);
    if (within > size || count > size - within) return {};
    return {blocks[index].get() + within, static_cast<size_t>(count)};
  }

  std::optional<uint64_t> place(uint64_t offset, uint64_t count) const {
    if (block_bytes == 0) return std::nullopt;
    uint64_t index = offset / block_bytes;
    uint64_t within = offset % block_bytes;
    while (index < blocks.size()) {
      const uint64_t size = blockSize(index);
      if (within <= size && count <= size - within) return index * block_bytes + within;
      ++index;
      within = 0;
    }
    return std::nullopt;
  }

  CapturePlan plan;
  uint64_t block_bytes = 0;
  uint64_t arena_bytes = 0;
  std::vector<std::shared_ptr<uint8_t[]>> blocks;
};

inline uint64_t guaranteedSamples(const Reservation& reservation) {
  const CapturePlan& plan = reservation.plan;
  uint64_t cursor = 0;
  uint64_t samples = 0;
  for (uint64_t i = 0; i < plan.slots(); ++i) {
    const uint64_t per_channel = (plan.slotSamples(i) + 7) / 8;
    uint64_t trial = cursor;
    bool whole_window_fits = true;
    for (uint32_t c = 0; c < plan.channels(); ++c) {
      const auto at = reservation.place(trial, per_channel);
      if (!at) {
        whole_window_fits = false;
        break;
      }
      trial = *at + per_channel;
    }
    if (!whole_window_fits) break;
    cursor = trial;
    samples += plan.slotSamples(i);
  }
  return samples;
}

inline std::string noWindowRefusal(const CapturePlan& plan, uint64_t budget_bytes) {
  return std::format(
      "memory budget of {} bytes holds less than one window of {} bytes ({} channels x {} samples); lower "
      "rate or channel count, or raise --memory-budget",
      budget_bytes, plan.slotBytes(0), plan.channels(), plan.slotSamples(0));
}

struct ArenaShape {
  MemoryBudget budget;
  std::optional<std::string> cut;
  uint64_t arena_target = 0;
  uint64_t block_bytes = 0;
  uint64_t block_count = 0;
};

inline std::expected<ArenaShape, std::string> shapeArena(const CapturePlan& plan, const MemoryBudget& budget,
                                                         const AllocatorSettings& settings) {
  ArenaShape shape;
  shape.budget = budget;
  if (budget.cut_from) {
    shape.cut = std::format("free RAM fell after session's memory was planned: this device's share of {} bytes is "
                            "cut to {} bytes",
                            *budget.cut_from, budget.bytes);
  }

  const uint64_t asked_block_bytes =
      settings.arena_block_bytes != 0 ? settings.arena_block_bytes : kArenaBlockBytes;
  const uint64_t cap_bytes = settings.arena_cap == ArenaCap::WholeSlotsWithRingMargin
                                 ? triggeredArenaCap(plan, asked_block_bytes)
                                 : plan.totalBytes();
  shape.arena_target = (std::min)(budget.bytes, cap_bytes);

  if (shape.arena_target < plan.slotBytes(0)) {
    return std::unexpected(noWindowRefusal(plan, budget.bytes));
  }

  shape.block_bytes = (std::min)(asked_block_bytes, shape.arena_target);
  // WHY: No segment straddles a block boundary, so a block must hold the widest segment: one channel's raw window.
  shape.block_bytes = (std::max)(shape.block_bytes, (uint64_t{plan.samplesPerWindow()} + 7) / 8);
  shape.block_count = (shape.arena_target + shape.block_bytes - 1) / shape.block_bytes;
  return shape;
}

inline std::expected<ArenaShape, std::string> shapeArena(const CapturePlan& plan, MemoryProbe& probe,
                                                         const AllocatorSettings& settings) {
  auto budget = computeBudget(probe, settings.budget);
  if (!budget) return std::unexpected(budget.error());
  return shapeArena(plan, *budget, settings);
}

inline uint64_t withheldBytes(const MemoryBudget& budget) {
  // WHY: Withhold held_back too: memory the capture builds after its arena (pool, scratch, index, ring) must still
  // be there when it is built.
  return budget.held_back > (std::numeric_limits<uint64_t>::max)() - budget.reserve
             ? (std::numeric_limits<uint64_t>::max)()
             : budget.reserve + budget.held_back;
}

inline MemoryBudget withHeldBackStillToCome(const MemoryBudget& budget, uint64_t free_now, uint64_t arena_taken) {
  // WHY: Held back is resident before the arena grows; a fall beyond the arena is it: withhold only the rest.
  const uint64_t fell = budget.available > free_now ? budget.available - free_now : 0;
  const uint64_t beyond_arena = fell > arena_taken ? fell - arena_taken : 0;
  MemoryBudget still_held = budget;
  still_held.held_back -= (std::min)(budget.held_back, beyond_arena);
  return still_held;
}

inline std::optional<std::string> freeRamRefusalAt(uint64_t free_now, const MemoryBudget& budget, uint64_t index,
                                                   uint64_t bytes) {
  const uint64_t reserve = budget.reserve;
  const uint64_t held_back = budget.held_back;
  const uint64_t withheld = withheldBytes(budget);
  if (free_now >= withheld && free_now - withheld >= bytes) return std::nullopt;
  return held_back != 0 ? std::format("free RAM fell to {} bytes before block {}; reserve of {} bytes plus {} bytes "
                                      "held back for what capture takes outside its arena are kept",
                                      free_now, index, reserve, held_back)
                        : std::format("free RAM fell to {} bytes before block {}; reserve of {} bytes is kept",
                                      free_now, index, reserve);
}

inline std::optional<std::string> freeRamRefusal(MemoryProbe& probe, const MemoryBudget& budget, uint64_t index,
                                                 uint64_t bytes, std::optional<uint64_t> arena_taken = std::nullopt) {
  auto free_now = probe.freePhysicalBytes();
  if (!free_now) return std::format("could not re-read free RAM before block {}: {}", index, free_now.error());
  if (!arena_taken) return freeRamRefusalAt(*free_now, budget, index, bytes);
  return freeRamRefusalAt(*free_now, withHeldBackStillToCome(budget, *free_now, *arena_taken), index, bytes);
}

inline std::expected<Reservation, std::string> allocateCapture(
    const CapturePlan& plan, MemoryProbe& probe, BlockSource& source,
    const AllocatorSettings& settings) {
  auto shape = shapeArena(plan, probe, settings);
  if (!shape) return std::unexpected(shape.error());
  const MemoryBudget& budget = shape->budget;
  const uint64_t arena_target = shape->arena_target;
  const uint64_t block_bytes = shape->block_bytes;
  const uint64_t block_count = shape->block_count;

  Reservation reservation;
  reservation.plan = plan;
  reservation.slotsRequested = plan.slots();
  reservation.block_bytes = block_bytes;
  const uint64_t every = (std::max)(settings.reread_every_blocks, uint32_t{1});
  std::vector<std::span<uint8_t>> uncleared;
  try {
    reservation.blocks.reserve(static_cast<size_t>(block_count));
    uncleared.reserve(static_cast<size_t>((std::min)(every, block_count)));
  } catch (const std::bad_alloc&) {
    return std::unexpected("out of memory tracking capture's arena");
  }
  SystemClearingThreads system_threads;
  ClearingThreads& clearing_threads =
      settings.clearing_threads != nullptr ? *settings.clearing_threads : system_threads;
  const auto clearTaken = [&] {
    clearBlocks(uncleared, settings.clearing_workers, clearing_threads);
    uncleared.clear();
  };

  std::string stopped;
  for (uint64_t i = 0; i < block_count; ++i) {
    const uint64_t bytes = (std::min)(block_bytes, arena_target - i * block_bytes);

    // WHY: Clear before the re-read: free RAM falls only as pages are written, so the reading must see them taken.
    if (i != 0 && i % every == 0) clearTaken();
    if (settings.policy == AllocationPolicy::Chunked && i != 0 && i % every == 0) {
      if (auto refused = freeRamRefusal(probe, budget, i, bytes)) {
        stopped = std::move(*refused);
        break;
      }
    }

    try {
      auto block = source.allocate(bytes);
      if (!block) {
        stopped = std::format("allocation failed at block {}: {}", i, block.error());
        break;
      }
      uncleared.emplace_back(block->get(), static_cast<size_t>(bytes));
      reservation.blocks.push_back(std::move(*block));
      reservation.arena_bytes += bytes;
    } catch (const std::bad_alloc&) {
      stopped = std::format("allocation failed at block {}: std::bad_alloc", i);
      break;
    }
  }
  clearTaken();

  reservation.samplesFloor = (std::min)(guaranteedSamples(reservation), plan.totalSamples());
  if (reservation.samplesFloor == 0) {
    if (!stopped.empty()) return std::unexpected(stopped);
    return std::unexpected(noWindowRefusal(plan, budget.bytes));
  }

  if (shape->cut) reservation.warnings.push_back(*shape->cut);
  if (!stopped.empty()) reservation.warnings.push_back(stopped);
  return reservation;
}

}
