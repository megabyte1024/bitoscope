/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

// WHY: Lock order: caller's mutex first, then pool's commit mutex; a worker never takes a caller's mutex.

#include <condition_variable>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "raw_capture_buffer.h"
#include "sample_window.h"

namespace bitoscope::capture {

struct PackPoolHooks {
  std::function<void(uint64_t seq)> before_pack;
  std::function<void(uint64_t seq)> after_packed;
  std::function<void(uint64_t seq, std::expected<PackedWindow, std::string>& packed)> alter_packed;
};

using PackPrepareFn = std::function<std::expected<void, std::string>(uint64_t seq, bitoscope::host::WindowLease& lease)>;

struct PackPoolConfig {
  uint32_t workers = 1;
  uint32_t depth = 0;
  PackPrepareFn prepare;
  PackPoolHooks hooks;
};

class PackPool {
 public:
  using CommitFn = std::function<void(const bitoscope::host::WindowInfo& info,
                                      const std::expected<PackedWindow, std::string>& packed)>;

  static std::expected<std::unique_ptr<PackPool>, std::string> create(
      const RawCaptureBuffer& buffer, PackPoolConfig config, CommitFn commit);

  ~PackPool();

  PackPool(const PackPool&) = delete;
  PackPool& operator=(const PackPool&) = delete;

  void drain();

  std::expected<uint64_t, bitoscope::host::WaitEnd> dispatch(bitoscope::host::WindowLease lease,
                                                             const bitoscope::host::WaitLimit& limit);
  std::expected<void, bitoscope::host::WaitEnd> drain(const bitoscope::host::WaitLimit& limit);

  void wakeWaiters();

  void abandon();

  void release();

  bool idle() const;

  std::unique_lock<std::mutex> lockCommits() const;

  uint32_t workers() const { return static_cast<uint32_t>(threads_.size()); }
  uint32_t depth() const { return static_cast<uint32_t>(slots_.size()); }
  uint64_t dispatched() const;
  uint64_t scratchBytes() const;
  uint64_t committed() const;

 private:
  enum class SlotState : uint8_t { Free, Queued, Packing, Packed };

  struct Slot {
    SlotState state = SlotState::Free;
    uint64_t seq = 0;
    bitoscope::host::WindowInfo info;
    bitoscope::host::WindowLease lease;
    PackScratch scratch;
    std::expected<PackedWindow, std::string> result;
  };

  PackPool(const RawCaptureBuffer& buffer, PackPoolHooks hooks, PackPrepareFn prepare, CommitFn commit);

  void run();
  void commitReady();
  void stop();

  const RawCaptureBuffer& buffer_;
  PackPoolHooks hooks_;
  PackPrepareFn prepare_;
  CommitFn commit_;
  std::vector<Slot> slots_;
  std::vector<std::thread> threads_;

  mutable std::mutex state_mutex_;
  std::condition_variable work_cv_;
  std::condition_variable slot_cv_;
  uint64_t next_dispatch_ = 0;
  uint64_t next_take_ = 0;
  uint64_t committed_ = 0;
  bool stopping_ = false;
  bool commit_halted_ = false;

  mutable std::mutex commit_mutex_;
};

}
