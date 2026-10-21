/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <optional>

namespace bitoscope::acquisition {

struct Progress {
  uint64_t stored_samples = 0;
  uint64_t requested_samples = 0;
  uint64_t memory_used_bytes = 0;
  uint64_t memory_bytes = 0;
  uint64_t ring_slots_in_use = 0;
  uint64_t ring_slots = 0;
  bool triggered = false;
  struct Armed {
    uint64_t waited_ms = 0;
    bool history_complete = false;
    uint64_t timeout_s = 0;
  };
  std::optional<Armed> armed;
};

}
