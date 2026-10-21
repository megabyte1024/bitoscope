/*
 * SPDX-License-Identifier: MIT
 */

#include "pack_pool.h"

#include <exception>
#include <format>
#include <new>
#include <optional>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace bitoscope::capture {

namespace {

std::expected<PackedWindow, std::string> packThrew() noexcept {
  try {
    throw;
  } catch (const std::exception& e) {
    try {
      return std::unexpected(std::format("packing it threw: {}", e.what()));
    } catch (...) {
    }
  } catch (...) {
    try {
      return std::unexpected(std::string("packing it threw something that is not exception"));
    } catch (...) {
    }
  }
  return std::unexpected(std::string());
}

}

std::expected<std::unique_ptr<PackPool>, std::string> PackPool::create(
    const RawCaptureBuffer& buffer, PackPoolConfig config, CommitFn commit) {
  if (config.workers == 0) return std::unexpected("pack pool needs at least one worker");
  if (!commit) return std::unexpected("pack pool needs commit function");
  const uint64_t depth = config.depth != 0 ? config.depth : packPoolDepth(config.workers);
  const CapturePlan& plan = buffer.plan();

  std::unique_ptr<PackPool> pool;
  try {
    pool.reset(new PackPool(buffer, std::move(config.hooks), std::move(commit)));
    pool->slots_.reserve(static_cast<size_t>(depth));
    for (uint64_t i = 0; i < depth; ++i) {
      auto scratch = PackScratch::create(plan.channels(), plan.samplesPerWindow(), buffer.hasPyramid());
      if (!scratch) {
        return std::unexpected(std::format("cannot make pack pool's {} in-flight windows: {}",
                                           depth, scratch.error()));
      }
      pool->slots_.emplace_back();
      pool->slots_.back().scratch = std::move(*scratch);
    }
  } catch (const std::bad_alloc&) {
    return std::unexpected(std::format(
        "out of memory making pack pool's {} in-flight windows of {} channels", depth,
        plan.channels()));
  } catch (const std::length_error&) {
    return std::unexpected(std::format(
        "cannot make {} in-flight windows: more memory than this platform can address", depth));
  }

  try {
    pool->threads_.reserve(config.workers);
    for (uint32_t i = 0; i < config.workers; ++i) {
      pool->threads_.emplace_back([p = pool.get()] { p->run(); });
    }
  } catch (const std::system_error& e) {
    pool->stop();
    return std::unexpected(
        std::format("cannot start pack pool's {} workers: {}", config.workers, e.what()));
  } catch (const std::bad_alloc&) {
    pool->stop();
    return std::unexpected(
        std::format("out of memory starting pack pool's {} workers", config.workers));
  }
  return pool;
}

PackPool::PackPool(const RawCaptureBuffer& buffer, PackPoolHooks hooks, CommitFn commit)
    : buffer_(buffer), hooks_(std::move(hooks)), commit_(std::move(commit)) {}

PackPool::~PackPool() { stop(); }

void PackPool::stop() {
  {
    std::lock_guard lock(state_mutex_);
    stopping_ = true;
  }
  work_cv_.notify_all();
  slot_cv_.notify_all();
  for (std::thread& t : threads_) {
    if (t.joinable()) t.join();
  }
}

void PackPool::abandon() {
  stop();
  std::lock_guard lock(state_mutex_);
  for (Slot& slot : slots_) {
    if (slot.state == SlotState::Queued || slot.state == SlotState::Packed) {
      slot.lease = bitoscope::host::WindowLease{};
    }
  }
}

void PackPool::release() {
  stop();
  threads_.clear();
  std::lock_guard lock(state_mutex_);
  for (Slot& slot : slots_) {
    slot.lease = bitoscope::host::WindowLease{};
    slot.scratch = PackScratch{};
    slot.result = std::unexpected(std::string());
  }
}

void PackPool::wakeWaiters() {
  { std::lock_guard lock(state_mutex_); }
  slot_cv_.notify_all();
}

std::expected<uint64_t, bitoscope::host::WaitEnd> PackPool::dispatch(
    bitoscope::host::WindowLease lease, const bitoscope::host::WaitLimit& limit) {
  using bitoscope::host::WaitEnd;
  std::unique_lock lock(state_mutex_);
  const uint64_t seq = next_dispatch_;
  Slot& slot = slots_[static_cast<size_t>(seq % slots_.size())];
  if (slot.state != SlotState::Free) {
    const uint64_t started = limit.startedAt();
    for (;;) {
      slot_cv_.wait_for(lock, limit.poll, [&] {
        return slot.state == SlotState::Free || stopping_ || limit.abandoned();
      });
      if (slot.state == SlotState::Free && !stopping_) break;
      if (stopping_ || limit.abandoned()) {
        lock.unlock();
        return std::unexpected(WaitEnd::Abandoned);
      }
      if (limit.expired(started)) {
        lock.unlock();
        return std::unexpected(WaitEnd::TimedOut);
      }
    }
  }
  if (stopping_) {
    lock.unlock();
    return std::unexpected(WaitEnd::Abandoned);
  }
  slot.seq = seq;
  slot.info = lease.valid() ? lease.info() : bitoscope::host::WindowInfo{};
  slot.lease = std::move(lease);
  slot.state = SlotState::Queued;
  ++next_dispatch_;
  lock.unlock();
  work_cv_.notify_one();
  return seq;
}

std::expected<void, bitoscope::host::WaitEnd> PackPool::drain(
    const bitoscope::host::WaitLimit& limit) {
  using bitoscope::host::WaitEnd;
  std::unique_lock lock(state_mutex_);
  std::optional<uint64_t> started;
  for (;;) {
    if (committed_ == next_dispatch_) return {};
    if (stopping_ || limit.abandoned()) return std::unexpected(WaitEnd::Abandoned);
    if (!started) started = limit.startedAt();
    if (limit.expired(*started)) return std::unexpected(WaitEnd::TimedOut);
    slot_cv_.wait_for(lock, limit.poll, [&] {
      return committed_ == next_dispatch_ || stopping_ || limit.abandoned();
    });
  }
}

void PackPool::run() {
  for (;;) {
    Slot* slot = nullptr;
    uint64_t seq = 0;
    {
      std::unique_lock lock(state_mutex_);
      work_cv_.wait(lock, [&] { return stopping_ || next_take_ < next_dispatch_; });
      if (stopping_) return;
      seq = next_take_++;
      slot = &slots_[static_cast<size_t>(seq % slots_.size())];
      slot->state = SlotState::Packing;
    }

    // WHY: Catch everything: std::format throws bad_alloc when memory is short, and an escape would terminate.
    try {
      if (hooks_.before_pack) hooks_.before_pack(seq);
      slot->result = buffer_.packWindow(slot->lease, slot->scratch);
      if (hooks_.alter_packed) hooks_.alter_packed(seq, slot->result);
    } catch (...) {
      slot->result = packThrew();
    }
    // WHY: Lease stays in the slot until commit: a segment stored raw is a span over it, copied by the commit.
    {
      std::lock_guard lock(state_mutex_);
      slot->state = SlotState::Packed;
    }
    try {
      if (hooks_.after_packed) hooks_.after_packed(seq);
    } catch (...) {
    }
    commitReady();
  }
}

void PackPool::commitReady() {
  std::lock_guard commit(commit_mutex_);
  for (;;) {
    Slot* slot = nullptr;
    {
      std::lock_guard lock(state_mutex_);
      if (stopping_ || commit_halted_) return;
      Slot& next = slots_[static_cast<size_t>(committed_ % slots_.size())];
      if (next.state != SlotState::Packed || next.seq != committed_) return;
      slot = &next;
    }
    try {
      commit_(slot->info, slot->result);
    } catch (...) {
      std::lock_guard lock(state_mutex_);
      commit_halted_ = true;
      return;
    }
    slot->lease = bitoscope::host::WindowLease{};
    {
      std::lock_guard lock(state_mutex_);
      slot->state = SlotState::Free;
      ++committed_;
    }
    slot_cv_.notify_all();
  }
}

void PackPool::drain() {
  std::unique_lock lock(state_mutex_);
  slot_cv_.wait(lock, [&] { return stopping_ || committed_ == next_dispatch_; });
}

bool PackPool::idle() const {
  std::lock_guard lock(state_mutex_);
  return committed_ == next_dispatch_;
}

std::unique_lock<std::mutex> PackPool::lockCommits() const {
  return std::unique_lock<std::mutex>(commit_mutex_);
}

uint64_t PackPool::dispatched() const {
  std::lock_guard lock(state_mutex_);
  return next_dispatch_;
}

uint64_t PackPool::scratchBytes() const {
  std::lock_guard lock(state_mutex_);
  uint64_t bytes = 0;
  for (const Slot& slot : slots_) bytes += slot.scratch.heldBytes();
  return bytes;
}

uint64_t PackPool::committed() const {
  std::lock_guard lock(state_mutex_);
  return committed_;
}

}
