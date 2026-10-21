/*
 * SPDX-License-Identifier: MIT
 */

#include "block_clearing.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <exception>
#include <vector>

namespace bitoscope::capture {

namespace {

uint64_t piecesOf(std::span<uint8_t> block) {
  return (uint64_t{block.size()} + kClearingPieceBytes - 1) / kClearingPieceBytes;
}

class PieceQueue {
 public:
  explicit PieceQueue(std::span<const std::span<uint8_t>> blocks) : blocks_(blocks) {}

  void clearUntilEmpty() {
    size_t block = 0;
    uint64_t first_piece_of_block = 0;
    for (;;) {
      const uint64_t piece = next_piece_.fetch_add(1, std::memory_order_relaxed);
      while (block < blocks_.size() && piece >= first_piece_of_block + piecesOf(blocks_[block])) {
        first_piece_of_block += piecesOf(blocks_[block]);
        ++block;
      }
      if (block == blocks_.size()) return;
      const uint64_t offset = (piece - first_piece_of_block) * kClearingPieceBytes;
      const uint64_t bytes = (std::min)(kClearingPieceBytes, uint64_t{blocks_[block].size()} - offset);
      std::memset(blocks_[block].data() + offset, 0, static_cast<size_t>(bytes));
    }
  }

 private:
  std::span<const std::span<uint8_t>> blocks_;
  std::atomic<uint64_t> next_piece_{0};
};

class JoinedThreads {
 public:
  JoinedThreads() = default;
  JoinedThreads(const JoinedThreads&) = delete;
  JoinedThreads& operator=(const JoinedThreads&) = delete;
  ~JoinedThreads() {
    for (std::thread& thread : threads_) {
      if (thread.joinable()) thread.join();
    }
  }

  void reserve(size_t count) { threads_.reserve(count); }
  void add(std::thread thread) { threads_.push_back(std::move(thread)); }

 private:
  std::vector<std::thread> threads_;
};

}

std::thread SystemClearingThreads::start(std::function<void()> work) { return std::thread(std::move(work)); }

void clearBlocks(std::span<const std::span<uint8_t>> blocks, uint32_t workers, ClearingThreads& threads) {
  uint64_t pieces = 0;
  for (std::span<uint8_t> block : blocks) pieces += piecesOf(block);
  if (pieces == 0) return;
  const uint64_t wanted = (std::min)(uint64_t{workers}, pieces);

  PieceQueue queue(blocks);
  // WHY: Declared after queue, which the threads read: its destructor joins them before queue is destroyed.
  JoinedThreads started;
  try {
    started.reserve(static_cast<size_t>(wanted));
    for (uint64_t i = 1; i < wanted; ++i) started.add(threads.start([&queue] { queue.clearUntilEmpty(); }));
  } catch (const std::exception&) {
  }
  queue.clearUntilEmpty();
}

void clearBlocks(std::span<const std::span<uint8_t>> blocks, uint32_t workers) {
  SystemClearingThreads threads;
  clearBlocks(blocks, workers, threads);
}

}
