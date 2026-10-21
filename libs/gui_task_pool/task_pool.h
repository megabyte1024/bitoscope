/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace bitoscope::gui_task_pool {

struct TaskFailure {
  std::uint32_t lane = 0;
  std::string reason;
};

class TaskPool {
 public:
  using LaneBody = std::function<void(std::uint32_t lane)>;

  static std::expected<std::unique_ptr<TaskPool>, std::string> create(std::uint32_t workers);

  ~TaskPool();

  TaskPool(const TaskPool&) = delete;
  TaskPool& operator=(const TaskPool&) = delete;

  std::uint32_t workers() const { return static_cast<std::uint32_t>(threads_.size()); }

  std::uint32_t lanes() const { return workers() + 1; }

  std::vector<TaskFailure> runLanes(std::uint32_t lanes, const LaneBody& body);

  static void failNextCreateAfterForTest(std::uint32_t after);

  static std::uint32_t liveWorkerThreadsForTest();

  static void delayWorkerExitForTest(std::chrono::milliseconds delay);

 private:
  TaskPool() = default;

  void loop(std::uint32_t worker);
  void stopAndJoin();
  // WHY: Catches every exception: one leaving a worker's thread function would end the whole process.
  void runOneLane(std::uint32_t lane);

  std::vector<std::thread> threads_;

  mutable std::mutex mutex_;
  std::condition_variable wake_;
  std::condition_variable done_;
  const LaneBody* body_ = nullptr;
  std::vector<TaskFailure> failures_;
  std::uint64_t batch_ = 0;
  std::uint32_t activeLanes_ = 0;
  std::size_t outstanding_ = 0;
  bool stopping_ = false;
};

}
