/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "device_host.h"
#include "gui_event_queue.h"

namespace bitoscope::gui_devices {

class DeviceWorker {
 public:
  DeviceWorker(std::unique_ptr<DeviceHost> host, bitoscope::gui_events::GuiEventQueue& queue);
  ~DeviceWorker();
  DeviceWorker(const DeviceWorker&) = delete;
  DeviceWorker& operator=(const DeviceWorker&) = delete;

  void discover();

  void relist(std::string driver);

  void open(std::string driver, std::string deviceId);

  void listAgain(std::string driver);

  void waitIdle();

  std::size_t pendingJobs() const;

  void shutdown();

 private:
  void post(std::function<void(DeviceHost& host)> job);
  void run();

  std::unique_ptr<DeviceHost> host_;
  bitoscope::gui_events::GuiEventQueue& queue_;
  mutable std::mutex mutex_;
  std::condition_variable wake_;
  std::condition_variable idle_;
  std::deque<std::function<void(DeviceHost&)>> jobs_;
  bool running_ = false;
  bool stopping_ = false;
  bool shutDown_ = false;
  std::thread thread_;
};

}
