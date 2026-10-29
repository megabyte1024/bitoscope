/*
 * SPDX-License-Identifier: MIT
 */

#include "block_clearing.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <exception>
#include <vector>

#if defined(__linux__)
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace bitoscope::capture {

namespace {

enum class PieceWork : uint8_t { Clear, Commit };

void commitPiece(std::span<uint8_t> block, std::span<uint8_t> piece) {
#if defined(__linux__) && defined(MADV_POPULATE_WRITE)
  static const uintptr_t page = static_cast<uintptr_t>(sysconf(_SC_PAGESIZE));
  const uintptr_t inner_first = (reinterpret_cast<uintptr_t>(block.data()) + page - 1) / page * page;
  const uintptr_t inner_last = (reinterpret_cast<uintptr_t>(block.data()) + block.size()) / page * page;
  const uintptr_t begin = reinterpret_cast<uintptr_t>(piece.data());
  const uintptr_t end = begin + piece.size();
  const uintptr_t first = (std::max)(begin / page * page, inner_first);
  const uintptr_t last = (std::min)((end + page - 1) / page * page, inner_last);
  if (first < last && madvise(reinterpret_cast<void*>(first), last - first, MADV_POPULATE_WRITE) == 0) {
    if (begin < inner_first) std::memset(piece.data(), 0, (std::min)(end, inner_first) - begin);
    if (end > inner_last) {
      const uintptr_t from = (std::max)(begin, inner_last);
      std::memset(reinterpret_cast<uint8_t*>(from), 0, end - from);
    }
    return;
  }
#else
  (void)block;
#endif
  std::memset(piece.data(), 0, piece.size());
}

uint64_t piecesOf(std::span<uint8_t> block) {
  return (uint64_t{block.size()} + kClearingPieceBytes - 1) / kClearingPieceBytes;
}

class PieceQueue {
 public:
  PieceQueue(std::span<const std::span<uint8_t>> blocks, PieceWork work) : blocks_(blocks), work_(work) {}

  void workUntilEmpty() {
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
      const std::span<uint8_t> part = blocks_[block].subspan(static_cast<size_t>(offset), static_cast<size_t>(bytes));
      if (work_ == PieceWork::Commit) {
        commitPiece(blocks_[block], part);
      } else {
        std::memset(part.data(), 0, part.size());
      }
    }
  }

 private:
  std::span<const std::span<uint8_t>> blocks_;
  PieceWork work_ = PieceWork::Clear;
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

void workBlocks(std::span<const std::span<uint8_t>> blocks, uint32_t workers, ClearingThreads& threads,
                PieceWork work) {
  uint64_t pieces = 0;
  for (std::span<uint8_t> block : blocks) pieces += piecesOf(block);
  if (pieces == 0) return;
  const uint64_t wanted = (std::min)(uint64_t{workers}, pieces);

  PieceQueue queue(blocks, work);
  // WHY: Declared after queue, which the threads read: its destructor joins them before queue is destroyed.
  JoinedThreads started;
  try {
    started.reserve(static_cast<size_t>(wanted));
    for (uint64_t i = 1; i < wanted; ++i) started.add(threads.start([&queue] { queue.workUntilEmpty(); }));
  } catch (const std::exception&) {
  }
  queue.workUntilEmpty();
}

}

std::thread SystemClearingThreads::start(std::function<void()> work) { return std::thread(std::move(work)); }

void clearBlocks(std::span<const std::span<uint8_t>> blocks, uint32_t workers, ClearingThreads& threads) {
  workBlocks(blocks, workers, threads, PieceWork::Clear);
}

void clearBlocks(std::span<const std::span<uint8_t>> blocks, uint32_t workers) {
  SystemClearingThreads threads;
  clearBlocks(blocks, workers, threads);
}

void commitBlocks(std::span<const std::span<uint8_t>> blocks, uint32_t workers, ClearingThreads& threads) {
  workBlocks(blocks, workers, threads, PieceWork::Commit);
}

}
