/*
 * SPDX-License-Identifier: MIT
 */

#include "device_worker.h"

#include <utility>

#include "device_watcher_adapter.h"

namespace bitoscope::gui_devices {

namespace ge = bitoscope::gui_events;

DeviceWorker::DeviceWorker(std::unique_ptr<DeviceHost> host, ge::GuiEventQueue& queue)
    : host_(std::move(host)), queue_(queue) {
  thread_ = std::thread([this] { run(); });
}

DeviceWorker::~DeviceWorker() {
  shutdown();
}

void DeviceWorker::post(std::function<void(DeviceHost& host)> job) {
  {
    std::lock_guard lock(mutex_);
    if (stopping_) {
      return;
    }
    jobs_.push_back(std::move(job));
  }
  wake_.notify_one();
}

void DeviceWorker::run() {
  for (;;) {
    std::function<void(DeviceHost&)> job;
    {
      std::unique_lock lock(mutex_);
      wake_.wait(lock, [this] { return stopping_ || !jobs_.empty(); });
      if (stopping_) {
        return;
      }
      job = std::move(jobs_.front());
      jobs_.pop_front();
      running_ = true;
    }
    job(*host_);
    {
      std::lock_guard lock(mutex_);
      running_ = false;
    }
    idle_.notify_all();
  }
}

void DeviceWorker::discover() {
  post([this](DeviceHost& host) {
    const std::filesystem::path directory = host.driverDirectory();
    const std::vector<std::string> drivers = host.drivers();
    queue_.post([directory, drivers](const ge::GuiEventHandlers& handlers) {
      if (handlers.onDriversFound) handlers.onDriversFound(directory, drivers);
    });
    for (const std::string& driver : drivers) {
      auto list = host.listDevices(driver);
      const bool watch = list && list->supportsWatch;
      queue_.post([driver, list](const ge::GuiEventHandlers& handlers) {
        if (handlers.onDevicesListed) handlers.onDevicesListed(driver, list);
      });
      if (!watch) {
        continue;
      }
      auto watched = host.watch(driver, ge::makePostingDeviceWatcherEvents(queue_, driver));
      if (!watched) {
        std::string why = "could not watch: " + watched.error();
        queue_.post([driver, why](const ge::GuiEventHandlers& handlers) {
          if (handlers.onWatchEnded) handlers.onWatchEnded(driver, why);
        });
      }
    }
  });
}

void DeviceWorker::relist(std::string driver) {
  post([this, driver = std::move(driver)](DeviceHost& host) {
    auto list = host.listDevices(driver);
    queue_.post([driver, list](const ge::GuiEventHandlers& handlers) {
      if (handlers.onDevicesListed) handlers.onDevicesListed(driver, list);
    });
  });
}

void DeviceWorker::open(std::string driver, std::string deviceId) {
  post([this, driver = std::move(driver), deviceId = std::move(deviceId)](DeviceHost& host) {
    auto opened = host.open(driver, deviceId);
    queue_.post([driver, deviceId, opened](const ge::GuiEventHandlers& handlers) {
      if (handlers.onDeviceOpened) handlers.onDeviceOpened(driver, deviceId, opened);
    });
  });
}

void DeviceWorker::listAgain(std::string driver) {
  post([driver = std::move(driver)](DeviceHost& host) { static_cast<void>(host.listAgain(driver)); });
}

void DeviceWorker::waitIdle() {
  std::unique_lock lock(mutex_);
  idle_.wait(lock, [this] { return stopping_ || (jobs_.empty() && !running_); });
  idle_.wait(lock, [this] { return !running_; });
}

std::size_t DeviceWorker::pendingJobs() const {
  std::lock_guard lock(mutex_);
  return jobs_.size();
}

void DeviceWorker::shutdown() {
  {
    std::lock_guard lock(mutex_);
    if (shutDown_) {
      return;
    }
    shutDown_ = true;
    stopping_ = true;
    jobs_.clear();
  }
  wake_.notify_all();
  idle_.notify_all();
  if (thread_.joinable()) {
    thread_.join();
  }
  if (host_) {
    host_->stopWatching();  // WHY: Watch threads must end before the host (and its drivers) is destroyed.
    host_.reset();
  }
  idle_.notify_all();
}

}
