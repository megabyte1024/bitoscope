/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstddef>
#include <cstdint>

namespace bitoscope::waveform_view {

class AnnotationMemory {
 public:
  virtual ~AnnotationMemory() = default;
  virtual bool acquire(std::size_t bytes) = 0;
  virtual void release(std::size_t bytes) = 0;
};

// WHY: AnnotationBudget is not synchronized: use it from one thread at a time.
class AnnotationBudget final : public AnnotationMemory {
 public:
  explicit AnnotationBudget(std::uint64_t limitBytes, std::uint64_t heldBytes = 0);
  bool acquire(std::size_t bytes) override;
  void release(std::size_t bytes) override;
  std::uint64_t limit() const { return limit_; }
  std::uint64_t held() const { return held_; }

 private:
  std::uint64_t limit_;
  std::uint64_t held_;
};

struct GroupsRefusal {
  enum class Reason { OverBudget, OutOfMemory };
  Reason reason = Reason::OverBudget;
  std::size_t stored = 0;
  bool operator==(const GroupsRefusal&) const = default;
};

}
