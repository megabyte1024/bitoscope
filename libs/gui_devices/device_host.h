/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

// WHY: Every DeviceHost method runs on the worker's one thread, never the UI thread; no locking needed.

#include <expected>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "device_watcher.h"
#include "ipc_messages.h"
#include "opened_device.h"

namespace bitoscope::gui_devices {

class DeviceHost {
 public:
  virtual ~DeviceHost() = default;

  virtual std::filesystem::path driverDirectory() const = 0;

  virtual std::vector<std::string> drivers() = 0;

  virtual std::expected<bitoscope::ipc::DeviceList, std::string> listDevices(std::string_view driver) = 0;

  virtual std::expected<void, std::string> watch(std::string_view driver,
                                                 bitoscope::host::DeviceWatcherEvents events) = 0;

  virtual std::expected<bitoscope::host::OpenedDevice, bitoscope::host::OpenRefusal> open(
      std::string_view driver, std::string_view deviceId) = 0;

  virtual std::expected<void, std::string> listAgain(std::string_view driver) = 0;

  virtual void stopWatching() = 0;
};

}
