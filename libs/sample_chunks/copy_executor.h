/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

// WHY: run() must not return before every slice has returned: the source bytes belong to a ring slot
// that is valid only until submit() returns.

#include <cstddef>
#include <cstdint>

namespace bitoscope::host {

class CopyExecutor {
 public:
  using SliceFn = void (*)(void* ctx, size_t slice);

  virtual ~CopyExecutor() = default;

  virtual void run(size_t num_slices, SliceFn fn, void* ctx) = 0;
};

class InlineCopyExecutor final : public CopyExecutor {
 public:
  void run(size_t num_slices, SliceFn fn, void* ctx) override {
    for (size_t i = 0; i < num_slices; ++i) fn(ctx, i);
  }
};

struct SliceRange {
  uint32_t first = 0;
  uint32_t count = 0;
};

struct SlicePlan {
  static constexpr uint32_t kAlign = 64;

  static constexpr size_t count(uint32_t samples, uint32_t slice_samples) {
    if (samples == 0) return 0;
    if (slice_samples == 0 || samples <= slice_samples) return 1;
    return (static_cast<size_t>(samples) + slice_samples - 1) / slice_samples;
  }

  static constexpr SliceRange range(size_t index, uint32_t samples, uint32_t slice_samples) {
    if (slice_samples == 0 || samples <= slice_samples) return SliceRange{0, samples};
    const uint32_t first = static_cast<uint32_t>(index) * slice_samples;
    const uint32_t left = samples - first;
    return SliceRange{first, left < slice_samples ? left : slice_samples};
  }
};

}
