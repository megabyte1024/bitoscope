/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "annotation_memory.h"
#include "decoder.h"

namespace bitoscope::gui_decode {

class MemoryShare;

class LockedMemory {
 public:
  explicit LockedMemory(std::shared_ptr<bitoscope::waveform_view::AnnotationMemory> counter);

 private:
  friend class MemoryShare;
  std::mutex mutex_;
  std::shared_ptr<bitoscope::waveform_view::AnnotationMemory> counter_;
  std::vector<MemoryShare*> snapshots_;
};

// WHY: A refusing build gives back its bytes in one locked step: no other build is refused for bytes being freed.
// Snapshot shares give way to Result shares: a progress picture must never fail a decoder's result.
class MemoryShare final : public bitoscope::waveform_view::AnnotationMemory {
 public:
  enum class Kind { Result, Snapshot };
  explicit MemoryShare(LockedMemory& memory, Kind kind = Kind::Result);
  ~MemoryShare() override;
  MemoryShare(const MemoryShare&) = delete;
  MemoryShare& operator=(const MemoryShare&) = delete;
  bool acquire(std::size_t bytes) override;
  void release(std::size_t bytes) override;
  void giveBack();

 private:
  void giveBackLocked();

  LockedMemory& memory_;
  Kind kind_;
  std::uint64_t held_ = 0;
  bool refused_ = false;
};

inline constexpr std::uint64_t kMiB = std::uint64_t{1} << 20;
// WHY: A floor, so a machine short of free memory at that instant still decodes an ordinary capture.
inline constexpr std::uint64_t kMinimumAutomaticBudgetBytes = 256 * kMiB;
inline constexpr std::uint64_t kMaximumBudgetMib = std::uint64_t{1} << 24;
inline constexpr const char* kBudgetEnvironmentVariable = "BITOSCOPE_DECODER_BUDGET_MIB";

std::optional<std::uint64_t> parseBudgetMib(std::string_view text);
std::optional<std::uint64_t> budgetMibFromEnvironment();

std::uint64_t decoderBudgetBytes(std::optional<std::uint64_t> environmentMib, std::uint64_t preferenceMib,
                                 std::optional<std::uint64_t> freeBytes);

std::string budgetFailureText(const bitoscope::decode::Decoder& decoder,
                              const bitoscope::waveform_view::GroupsRefusal& refusal, std::uint64_t budgetBytes);

}
