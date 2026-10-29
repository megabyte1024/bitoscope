/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <vector>

namespace bitoscope::host {

struct WaitLimit {
  const std::atomic<bool>* abandon = nullptr;
  std::function<uint64_t()> now_ms;
  uint64_t timeout_ms = 0;
  std::chrono::milliseconds poll{10};

  bool abandoned() const { return abandon != nullptr && abandon->load(std::memory_order_acquire); }
  uint64_t startedAt() const { return now_ms ? now_ms() : 0; }
  bool expired(uint64_t started_ms) const {
    if (!now_ms) return false;
    const uint64_t now = now_ms();
    return now >= started_ms && now - started_ms >= timeout_ms;
  }
};

enum class WaitEnd { Abandoned, TimedOut };

struct WindowInfo {
  uint64_t first_sample_index = 0;
  uint64_t sample_rate_milli_hz = 0;
  uint32_t num_channels = 0;
  uint32_t num_samples = 0;
};

class WindowPool;

class WindowLease {
 public:
  WindowLease() = default;

  WindowLease(WindowLease&& other) noexcept
      : pool_(other.pool_), slot_(other.slot_) {
    other.pool_ = nullptr;
  }

  WindowLease& operator=(WindowLease&& other) noexcept {
    if (this != &other) {
      release();
      pool_ = other.pool_;
      slot_ = other.slot_;
      other.pool_ = nullptr;
    }
    return *this;
  }

  WindowLease(const WindowLease&) = delete;
  WindowLease& operator=(const WindowLease&) = delete;

  ~WindowLease() { release(); }

  bool valid() const { return pool_ != nullptr; }

  const WindowInfo& info() const;
  WindowInfo& info();

  std::span<const uint8_t> channel(uint32_t index) const;
  std::span<uint8_t> channel(uint32_t index);

 private:
  friend class WindowPool;
  WindowLease(WindowPool* pool, size_t slot) : pool_(pool), slot_(slot) {}

  void release();

  WindowPool* pool_ = nullptr;
  size_t slot_ = 0;
};

class WindowPool {
 public:
  WindowPool(uint32_t num_buffers, uint32_t num_channels, uint32_t samples_per_window)
      : num_channels_(num_channels),
        bytes_per_channel_((samples_per_window + 7) / 8),
        slot_bytes_(static_cast<size_t>(bytes_per_channel_) * num_channels_),
        slots_(num_buffers) {
    for (Slot& slot : slots_) slot.storage = std::make_unique_for_overwrite<uint8_t[]>(slot_bytes_);
  }

  WindowPool(const WindowPool&) = delete;
  WindowPool& operator=(const WindowPool&) = delete;

  // WHY: Lock-free on purpose: release() runs on any consumer thread, and a mutex here could make acquire() wait.
  std::optional<WindowLease> acquire() {
    if (released_.load(std::memory_order_acquire)) return std::nullopt;
    for (size_t i = 0; i < slots_.size(); ++i) {
      if (slots_[i].in_use.load(std::memory_order_relaxed)) continue;
      if (!slots_[i].in_use.exchange(true, std::memory_order_acquire)) {
        slots_[i].info = WindowInfo{};
        return WindowLease(this, i);
      }
    }
    return std::nullopt;
  }

  // WHY: No lost wake-up: release() frees slot, bumps releases_, then notifies if a waiter announced itself;
  // the waiter reads releases_ before its scan and again under wait_mutex_.
  std::expected<WindowLease, WaitEnd> acquireWaiting(const WaitLimit& limit = {}) {
    std::optional<uint64_t> started;
    for (;;) {
      const uint32_t seen = releases_.load(std::memory_order_seq_cst);
      if (auto lease = acquire()) return std::move(*lease);
      if (limit.abandoned() || released_.load(std::memory_order_acquire)) return std::unexpected(WaitEnd::Abandoned);
      if (!started) started = limit.startedAt();
      if (limit.expired(*started)) return std::unexpected(WaitEnd::TimedOut);
      waiters_.fetch_add(1, std::memory_order_seq_cst);
      {
        std::unique_lock lock(wait_mutex_);
        wait_cv_.wait_for(lock, limit.poll, [&] {
          return releases_.load(std::memory_order_seq_cst) != seen || limit.abandoned();
        });
      }
      waiters_.fetch_sub(1, std::memory_order_seq_cst);
    }
  }

  void wakeWaiters() {
    { std::lock_guard lock(wait_mutex_); }
    wait_cv_.notify_all();
  }

  uint32_t capacity() const { return static_cast<uint32_t>(slots_.size()); }

  void releaseStorage() {
    // WHY: Set released_ BEFORE freeing: a concurrent acquire() must not hand out a slot whose storage is gone.
    released_.store(true, std::memory_order_release);
    for (Slot& slot : slots_) {
      if (!slot.in_use.load(std::memory_order_acquire)) slot.storage.reset();
    }
  }

  uint64_t storageBytes() const {
    uint64_t bytes = 0;
    for (const Slot& slot : slots_) {
      if (slot.storage) bytes += slot_bytes_;
    }
    return bytes;
  }

  uint32_t available() const {
    uint32_t free_count = 0;
    for (const Slot& slot : slots_) {
      if (!slot.in_use.load(std::memory_order_relaxed)) ++free_count;
    }
    return free_count;
  }

  uint32_t numChannels() const { return num_channels_; }
  uint32_t bytesPerChannel() const { return bytes_per_channel_; }

 private:
  friend class WindowLease;

  struct Slot {
    std::unique_ptr<uint8_t[]> storage;
    WindowInfo info;
    std::atomic<bool> in_use{false};
  };

  // WHY: The slot is freed BEFORE releases_ is bumped: acquireWaiting()'s no-lost-wake-up argument needs that order.
  void release(size_t slot) {
    slots_[slot].in_use.store(false, std::memory_order_release);
    releases_.fetch_add(1, std::memory_order_seq_cst);
    if (waiters_.load(std::memory_order_seq_cst) != 0) wakeWaiters();
  }

  WindowInfo& infoOf(size_t slot) { return slots_[slot].info; }

  std::span<uint8_t> channelOf(size_t slot, uint32_t index) {
    return std::span<uint8_t>(slots_[slot].storage.get() + static_cast<size_t>(index) * bytes_per_channel_,
                              bytes_per_channel_);
  }

  uint32_t num_channels_;
  uint32_t bytes_per_channel_;
  size_t slot_bytes_;
  std::vector<Slot> slots_;
  std::atomic<uint32_t> releases_{0};
  std::atomic<uint32_t> waiters_{0};
  std::atomic<bool> released_{false};
  std::mutex wait_mutex_;
  std::condition_variable wait_cv_;
};

inline void WindowLease::release() {
  if (pool_ != nullptr) {
    pool_->release(slot_);
    pool_ = nullptr;
  }
}

inline const WindowInfo& WindowLease::info() const { return pool_->infoOf(slot_); }
inline WindowInfo& WindowLease::info() { return pool_->infoOf(slot_); }

inline std::span<const uint8_t> WindowLease::channel(uint32_t index) const {
  return pool_->channelOf(slot_, index);
}

inline std::span<uint8_t> WindowLease::channel(uint32_t index) {
  return pool_->channelOf(slot_, index);
}

}
