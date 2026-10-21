/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <functional>
#include <span>
#include <thread>

namespace bitoscope::capture {

inline constexpr uint64_t kClearingPieceBytes = 2ULL << 20;

class ClearingThreads {
 public:
  virtual ~ClearingThreads() = default;
  virtual std::thread start(std::function<void()> work) = 0;
};

class SystemClearingThreads final : public ClearingThreads {
 public:
  std::thread start(std::function<void()> work) override;
};

void clearBlocks(std::span<const std::span<uint8_t>> blocks, uint32_t workers, ClearingThreads& threads);
void clearBlocks(std::span<const std::span<uint8_t>> blocks, uint32_t workers);

}
