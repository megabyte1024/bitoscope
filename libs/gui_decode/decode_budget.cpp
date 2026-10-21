/*
 * SPDX-License-Identifier: MIT
 */

#include "decode_budget.h"

#include <algorithm>
#include <cstdlib>
#include <format>
#include <new>
#include <utility>

#include "decoder_labels.h"
#include "derivation_context.h"
#include "spec_display.h"

namespace bitoscope::gui_decode {

LockedMemory::LockedMemory(std::shared_ptr<waveform_view::AnnotationMemory> counter) : counter_(std::move(counter)) {}

MemoryShare::MemoryShare(LockedMemory& memory, Kind kind) : memory_(memory), kind_(kind) {
  if (kind_ != Kind::Snapshot) return;
  std::lock_guard<std::mutex> lock(memory_.mutex_);
  memory_.snapshots_.push_back(this);
}

MemoryShare::~MemoryShare() {
  if (kind_ != Kind::Snapshot) return;
  std::lock_guard<std::mutex> lock(memory_.mutex_);
  std::erase(memory_.snapshots_, this);
}

void MemoryShare::giveBack() {
  std::lock_guard<std::mutex> lock(memory_.mutex_);
  giveBackLocked();
}

void MemoryShare::giveBackLocked() {
  if (refused_) return;
  refused_ = true;
  memory_.counter_->release(static_cast<std::size_t>(held_));
}

bool MemoryShare::acquire(std::size_t bytes) {
  std::lock_guard<std::mutex> lock(memory_.mutex_);
  if (refused_) return false;
  try {
    bool granted = memory_.counter_->acquire(bytes);
    if (!granted && kind_ == Kind::Result) {
      for (MemoryShare* snapshot : memory_.snapshots_) snapshot->giveBackLocked();
      granted = memory_.counter_->acquire(bytes);
    }
    if (!granted) {
      giveBackLocked();
      return false;
    }
  } catch (const std::bad_alloc&) {
    giveBackLocked();
    throw;
  }
  held_ += bytes;
  return true;
}

void MemoryShare::release(std::size_t bytes) {
  std::lock_guard<std::mutex> lock(memory_.mutex_);
  held_ -= bytes;
  if (!refused_) memory_.counter_->release(bytes);
}

std::optional<std::uint64_t> parseBudgetMib(std::string_view text) {
  if (text.empty()) return std::nullopt;
  std::uint64_t mib = 0;
  for (const char c : text) {
    if (c < '0' || c > '9') return std::nullopt;
    mib = mib * 10 + static_cast<std::uint64_t>(c - '0');
    if (mib > kMaximumBudgetMib) return std::nullopt;
  }
  if (mib == 0) return std::nullopt;
  return mib;
}

std::optional<std::uint64_t> budgetMibFromEnvironment() {
#ifdef _MSC_VER
  // WHY: Use _dupenv_s, not getenv: MSVC deprecates getenv at /W4.
  char* value = nullptr;
  std::size_t length = 0;
  if (_dupenv_s(&value, &length, kBudgetEnvironmentVariable) != 0 || value == nullptr) return std::nullopt;
  const std::string text(value);
  std::free(value);
  return parseBudgetMib(text);
#else
  const char* value = std::getenv(kBudgetEnvironmentVariable);
  if (value == nullptr) return std::nullopt;
  return parseBudgetMib(value);
#endif
}

std::uint64_t decoderBudgetBytes(std::optional<std::uint64_t> environmentMib, std::uint64_t preferenceMib,
                                 std::optional<std::uint64_t> freeBytes) {
  if (environmentMib) return *environmentMib * kMiB;
  if (preferenceMib != 0 && preferenceMib <= kMaximumBudgetMib) return preferenceMib * kMiB;
  if (!freeBytes) return kMinimumAutomaticBudgetBytes;
  return (std::max)(*freeBytes / 2 / kMiB * kMiB, kMinimumAutomaticBudgetBytes);
}

std::string budgetFailureText(const decode::Decoder& decoder, const waveform_view::GroupsRefusal& refusal,
                              std::uint64_t budgetBytes) {
  const std::vector<std::string> forced = derivedRowEnableOptions(decoder);
  std::string advice;
  for (const decode::OptionSpec& option : decoder.options()) {
    if (option.name != "show_bits") continue;
    if (std::find(forced.begin(), forced.end(), option.name) != forced.end()) continue;
    advice = std::format("set {} to no, ", optionLabel(option));
  }
  advice += "bind fewer channels, shorten capture or ";
  const std::string name = decode::displayOf(decoder);
  if (refusal.reason == waveform_view::GroupsRefusal::Reason::OutOfMemory) {
    return std::format("{} result ran out of memory: stopped at {} annotations; {}close other programs", name,
                       refusal.stored, advice);
  }
  return std::format(
    "{} result needs more memory than decoder budget ({} MiB): stopped at {} annotations; {}raise "
    "decoderMemoryBudgetMib in preferences",
    name, budgetBytes / kMiB, refusal.stored, advice);
}

}
