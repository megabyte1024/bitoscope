/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "device_host.h"
#include "pipe_driver_host.h"

namespace bitoscope::gui_devices {

class PipeDeviceHost final : public DeviceHost {
 public:
  explicit PipeDeviceHost(bitoscope::host::PipeDriverHostOptions options);
  ~PipeDeviceHost() override;
  PipeDeviceHost(const PipeDeviceHost&) = delete;
  PipeDeviceHost& operator=(const PipeDeviceHost&) = delete;

  std::filesystem::path driverDirectory() const override;
  std::vector<std::string> drivers() override;
  std::expected<bitoscope::ipc::DeviceList, std::string> listDevices(std::string_view driver) override;
  std::expected<void, std::string> watch(std::string_view driver,
                                         bitoscope::host::DeviceWatcherEvents events) override;
  std::expected<bitoscope::host::OpenedDevice, bitoscope::host::OpenRefusal> open(
      std::string_view driver, std::string_view deviceId) override;
  std::expected<void, std::string> listAgain(std::string_view driver) override;
  void stopWatching() override;

  bitoscope::host::PipeDriverHost& driverHost() { return host_; }

 private:
  std::filesystem::path directory_;
  bitoscope::host::PipeDriverHost host_;
  std::mutex mutex_;
  std::vector<std::pair<std::string, std::unique_ptr<bitoscope::host::DeviceWatcher>>> watchers_;
};

}
