/*
 * SPDX-License-Identifier: MIT
 */

#include "chunk_deflater.h"

#include <algorithm>
#include <format>
#include <new>
#include <system_error>

#include <zlib.h>

namespace bitoscope::hdf5 {

std::expected<std::unique_ptr<ChunkDeflater>, std::string> ChunkDeflater::create(uint32_t workers, uint32_t level,
                                                                                 size_t chunk_bytes,
                                                                                 BeforeDeflate before_deflate) {
  if (workers == 0) return std::unexpected("deflate needs at least 1 worker");
  std::unique_ptr<ChunkDeflater> deflater(new ChunkDeflater(level, chunk_bytes, std::move(before_deflate)));
  try {
    const size_t bound = compressBound(static_cast<uLong>(chunk_bytes));
    deflater->slots_.resize(2 * static_cast<size_t>(workers));
    for (Slot& slot : deflater->slots_) {
      slot.raw.resize(chunk_bytes);
      slot.deflated.resize(bound);
    }
    for (uint32_t i = 0; i < workers; ++i) {
      deflater->threads_.emplace_back([raw = deflater.get()] { raw->workerLoop(); });
    }
  } catch (const std::bad_alloc&) {
    return std::unexpected("out of memory while starting deflate workers");
  } catch (const std::system_error& e) {
    return std::unexpected(std::format("cannot start deflate worker: {}", e.what()));
  }
  return deflater;
}

ChunkDeflater::ChunkDeflater(uint32_t level, size_t chunk_bytes, BeforeDeflate before_deflate)
    : level_(level), chunk_bytes_(chunk_bytes), before_deflate_(std::move(before_deflate)) {}

ChunkDeflater::~ChunkDeflater() {
  {
    const std::lock_guard<std::mutex> lock(mutex_);
    stopping_ = true;
  }
  work_.notify_all();
  for (std::thread& thread : threads_) thread.join();
}

void ChunkDeflater::deflateSlot(uint64_t chunk, Slot& slot) {
  try {
    if (before_deflate_) {
      if (auto r = before_deflate_(chunk); !r) {
        slot.error = r.error();
        return;
      }
    }
    uLongf size = static_cast<uLongf>(slot.deflated.size());
    const int status = compress2(slot.deflated.data(), &size, slot.raw.data(), static_cast<uLong>(chunk_bytes_),
                                 static_cast<int>(level_));
    if (status != Z_OK) {
      slot.error = std::format("deflate of chunk {} failed with zlib status {}", chunk, status);
      return;
    }
    slot.deflated_size = size;
  } catch (const std::exception& e) {
    slot.error = std::format("deflate of chunk {} failed: {}", chunk, e.what());
  } catch (...) {
    slot.error = std::format("deflate of chunk {} failed", chunk);
  }
}

void ChunkDeflater::workerLoop() {
  std::unique_lock<std::mutex> lock(mutex_);
  for (;;) {
    work_.wait(lock, [this] { return stopping_ || taken_ < submitted_; });
    if (stopping_) return;
    const uint64_t chunk = taken_++;
    Slot& slot = slots_[chunk % slots_.size()];
    lock.unlock();
    deflateSlot(chunk, slot);
    lock.lock();
    slot.done = true;
    done_.notify_all();
  }
}

std::expected<void, std::string> ChunkDeflater::drainOldest(const Sink& sink) {
  Slot& slot = slots_[consumed_ % slots_.size()];
  {
    std::unique_lock<std::mutex> lock(mutex_);
    done_.wait(lock, [&slot] { return slot.done; });
  }
  if (!slot.error.empty()) return std::unexpected(slot.error);
  if (auto r = sink(consumed_, std::span<const uint8_t>(slot.deflated.data(), slot.deflated_size)); !r) return r;
  slot.done = false;
  ++consumed_;
  return {};
}

std::expected<std::span<uint8_t>, std::string> ChunkDeflater::reserve(const Sink& sink) {
  while (submitted_ - consumed_ == slots_.size()) {
    if (auto r = drainOldest(sink); !r) return std::unexpected(r.error());
  }
  Slot& slot = slots_[submitted_ % slots_.size()];
  return std::span<uint8_t>(slot.raw.data(), slot.raw.size());
}

void ChunkDeflater::submit(size_t valid_bytes) {
  Slot& slot = slots_[submitted_ % slots_.size()];
  std::fill(slot.raw.begin() + static_cast<std::ptrdiff_t>(valid_bytes), slot.raw.end(), uint8_t{0});
  {
    const std::lock_guard<std::mutex> lock(mutex_);
    ++submitted_;
  }
  work_.notify_one();
}

std::expected<void, std::string> ChunkDeflater::finish(const Sink& sink) {
  while (consumed_ < submitted_) {
    if (auto r = drainOldest(sink); !r) return r;
  }
  return {};
}

}
