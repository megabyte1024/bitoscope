/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

// WHY: Default budget is capped at 60% of available: free RAM was verified only up to touching 60% of it.

#include <algorithm>
#include <cstdint>
#include <expected>
#include <functional>
#include <limits>
#include <optional>
#include <string>

#include "memory_probe.h"

namespace bitoscope::capture {

struct BudgetSettings {
  std::optional<uint64_t> explicit_budget{};
  std::optional<uint64_t> reserve{};
  uint64_t held_back = 0;
  bool fit_explicit_to_ceiling = false;
  std::function<uint64_t(uint64_t arena_bytes)> held_back_for_arena{};
};

struct MemoryBudget {
  uint64_t bytes = 0;
  uint64_t available = 0;
  uint64_t reserve = 0;
  uint64_t held_back = 0;
  std::optional<uint64_t> cut_from;
};

inline uint64_t defaultReserve(uint64_t available) { return available / 5; }

namespace budget_detail {

inline constexpr const char* kHeldBackParts =
    "what capture takes besides its arena: window pool, compression scratch, "
    "capture index and bulk ring's mapping";

inline constexpr uint64_t kMax = std::numeric_limits<uint64_t>::max();

inline uint64_t sixtyPercent(uint64_t value) {
  return (value / 100) * 60 + ((value % 100) * 60) / 100;
}

}

inline std::expected<MemoryBudget, std::string> computeBudget(MemoryProbe& probe,
                                                              const BudgetSettings& settings) {
  auto free_bytes = probe.freePhysicalBytes();
  if (!free_bytes) return std::unexpected(free_bytes.error());

  const uint64_t available = *free_bytes;
  if (available == 0) {
    return std::unexpected(
        "system reports 0 bytes of free RAM; nothing can be captured until memory is "
        "freed");
  }

  const uint64_t reserve = settings.reserve ? *settings.reserve : defaultReserve(available);
  if (reserve >= available) {
    return std::unexpected("reserve (" + std::to_string(reserve) +
                           " bytes) leaves nothing of " + std::to_string(available) +
                           " bytes of free RAM; lower --reserve or free memory");
  }
  const uint64_t after_reserve = available - reserve;

  const auto heldFor = [&settings](uint64_t arena_bytes) {
    const uint64_t linked = settings.held_back_for_arena ? settings.held_back_for_arena(arena_bytes) : 0;
    return linked > budget_detail::kMax - settings.held_back ? budget_detail::kMax : settings.held_back + linked;
  };
  const uint64_t held_back = heldFor(0);
  if (held_back >= after_reserve) {
    return std::unexpected(std::to_string(held_back) +
                           " bytes held back outside budget (" +
                           budget_detail::kHeldBackParts + ") leave nothing of " +
                           std::to_string(after_reserve) +
                           " bytes free after reserve of " + std::to_string(reserve) +
                           " out of " + std::to_string(available) +
                           " bytes of free RAM; use fewer channels, shorter capture, fewer "
                           "--pack-workers or smaller --ring-mib, or free memory");
  }
  uint64_t ceiling = 0;
  for (uint64_t hi = after_reserve - held_back; ceiling < hi;) {
    const uint64_t mid = ceiling + (hi - ceiling) / 2 + (hi - ceiling) % 2;
    const uint64_t held = heldFor(mid);
    if (held <= after_reserve && mid <= after_reserve - held) {
      ceiling = mid;
    } else {
      hi = mid - 1;
    }
  }

  MemoryBudget budget;
  budget.available = available;
  budget.reserve = reserve;
  if (settings.explicit_budget) {
    const uint64_t asked = *settings.explicit_budget;
    if (asked == 0) return std::unexpected("--memory-budget must be more than 0 bytes");
    if (asked > ceiling && settings.fit_explicit_to_ceiling) {
      budget.bytes = ceiling;
      budget.cut_from = asked;
      budget.held_back = heldFor(ceiling);
      return budget;
    }
    if (asked > ceiling) {
      const uint64_t held_at_ceiling = heldFor(ceiling);
      const std::string held_back_note =
          held_at_ceiling != 0 ? " and " + std::to_string(held_at_ceiling) +
                                     " held back outside budget (" +
                                     budget_detail::kHeldBackParts + ")"
                               : "";
      return std::unexpected("budget of " + std::to_string(asked) +
                             " bytes is more than this machine can give: at most " +
                             std::to_string(ceiling) + " bytes (" + std::to_string(available) +
                             " free minus reserve of " + std::to_string(reserve) +
                             held_back_note + ")");
    }
    budget.bytes = asked;
  } else {
    budget.bytes = (std::min)(ceiling, budget_detail::sixtyPercent(available));
    if (budget.bytes == 0) {
      return std::unexpected(std::to_string(available) +
                             " bytes of free RAM leaves budget of 0 bytes after reserve "
                             "of " +
                             std::to_string(reserve) +
                             "; free memory or pass --memory-budget");
    }
  }
  budget.held_back = heldFor(budget.bytes);
  return budget;
}

}
