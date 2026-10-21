/*
 * SPDX-License-Identifier: MIT
 */

#include "task_pool.h"

#include <atomic>
#include <exception>
#include <utility>

namespace bitoscope::gui_task_pool {

namespace {

// WHY: Exits are counted by the thread itself, not at join: a detached worker must still count while it runs.
std::atomic<std::uint32_t> gWorkersStarted{0};
std::atomic<std::uint32_t> gWorkersExited{0};

std::atomic<long long> gWorkerExitDelayMs{0};

constexpr std::uint32_t kNoFailure = 0xFFFFFFFFu;
std::atomic<std::uint32_t> gFailCreateAfter{kNoFailure};

std::string whatOf(const std::exception_ptr& error) {
  try {
    std::rethrow_exception(error);
  } catch (const std::exception& e) {
    return e.what();
  } catch (...) {
    return "it threw something carrying no message";
  }
}

}

std::expected<std::unique_ptr<TaskPool>, std::string> TaskPool::create(std::uint32_t workers) {
  const std::uint32_t failAfter = gFailCreateAfter.exchange(kNoFailure);
  std::unique_ptr<TaskPool> pool(new TaskPool());
  pool->threads_.reserve(workers);
  for (std::uint32_t w = 0; w < workers; ++w) {
    if (w >= failAfter) {
      pool->stopAndJoin();
      return std::unexpected("only " + std::to_string(w) + " of " + std::to_string(workers) +
                             " threads could be started");
    }
    try {
      pool->threads_.emplace_back([p = pool.get(), w] {
        p->loop(w);
        const long long delay = gWorkerExitDelayMs.load(std::memory_order_relaxed);
        if (delay > 0) std::this_thread::sleep_for(std::chrono::milliseconds(delay));
        gWorkersExited.fetch_add(1, std::memory_order_relaxed);
      });
      // WHY: Count the start here, not inside the thread: a thread that has not run yet is still a thread.
      gWorkersStarted.fetch_add(1, std::memory_order_relaxed);
    } catch (const std::exception& e) {
      pool->stopAndJoin();
      return std::unexpected(std::string("worker thread could not be started: ") + e.what());
    }
  }
  return pool;
}

TaskPool::~TaskPool() { stopAndJoin(); }

void TaskPool::stopAndJoin() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    stopping_ = true;
    ++batch_;
  }
  wake_.notify_all();
  for (std::thread& worker : threads_) {
    if (worker.joinable()) worker.join();
  }
  threads_.clear();
}

void TaskPool::runOneLane(std::uint32_t lane) {
  std::exception_ptr threw;
  try {
    (*body_)(lane);
  } catch (...) {
    threw = std::current_exception();
  }
  std::lock_guard<std::mutex> lock(mutex_);
  if (threw) failures_.push_back(TaskFailure{lane, whatOf(threw)});
  if (--outstanding_ == 0) done_.notify_all();
}

std::vector<TaskFailure> TaskPool::runLanes(std::uint32_t lanes, const LaneBody& body) {
  if (lanes == 0) lanes = 1;
  if (lanes > this->lanes()) lanes = this->lanes();
  if (!body) return {};
  {
    std::lock_guard<std::mutex> lock(mutex_);
    body_ = &body;
    failures_.clear();
    activeLanes_ = lanes;
    outstanding_ = lanes;
    ++batch_;
  }
  if (lanes > 1) wake_.notify_all();

  runOneLane(0);

  std::unique_lock<std::mutex> lock(mutex_);
  done_.wait(lock, [this] { return outstanding_ == 0; });
  body_ = nullptr;
  activeLanes_ = 0;
  std::vector<TaskFailure> failures = std::move(failures_);
  failures_.clear();
  lock.unlock();
  std::sort(failures.begin(), failures.end(),
            [](const TaskFailure& a, const TaskFailure& b) { return a.lane < b.lane; });
  return failures;
}

void TaskPool::loop(std::uint32_t worker) {
  const std::uint32_t lane = worker + 1;
  std::uint64_t seen = 0;
  for (;;) {
    bool mine = false;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      wake_.wait(lock, [&] { return batch_ != seen; });
      seen = batch_;
      if (stopping_) return;
      mine = lane < activeLanes_;
    }
    // WHY: A worker above the batch's lane count must NOT count itself down: outstanding_ counts only lanes that run.
    if (mine) runOneLane(lane);
  }
}

void TaskPool::failNextCreateAfterForTest(std::uint32_t after) { gFailCreateAfter.store(after); }

std::uint32_t TaskPool::liveWorkerThreadsForTest() {
  return gWorkersStarted.load(std::memory_order_relaxed) -
         gWorkersExited.load(std::memory_order_relaxed);
}

void TaskPool::delayWorkerExitForTest(std::chrono::milliseconds delay) {
  gWorkerExitDelayMs.store(delay.count(), std::memory_order_relaxed);
}

}
