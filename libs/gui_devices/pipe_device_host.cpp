/*
 * SPDX-License-Identifier: MIT
 */

#include "pipe_device_host.h"

#include <utility>

namespace bitoscope::gui_devices {

PipeDeviceHost::PipeDeviceHost(bitoscope::host::PipeDriverHostOptions options)
    : directory_(options.driverDirectory), host_(std::move(options)) {}

PipeDeviceHost::~PipeDeviceHost() {
  stopWatching();
}

std::filesystem::path PipeDeviceHost::driverDirectory() const {
  return directory_;
}

std::vector<std::string> PipeDeviceHost::drivers() {
  return host_.drivers();
}

std::expected<bitoscope::ipc::DeviceList, std::string> PipeDeviceHost::listDevices(
    std::string_view driver) {
  return host_.listDevices(driver);
}

std::expected<void, std::string> PipeDeviceHost::watch(std::string_view driver,
                                                       bitoscope::host::DeviceWatcherEvents events) {
  auto watcher = host_.watch(driver, std::move(events));
  if (!watcher) {
    return std::unexpected(watcher.error());
  }
  std::lock_guard lock(mutex_);
  watchers_.emplace_back(std::string(driver), std::move(*watcher));
  return {};
}

std::expected<void, std::string> PipeDeviceHost::listAgain(std::string_view driver) {
  std::lock_guard lock(mutex_);
  for (const auto& [name, watcher] : watchers_) {
    if (name == driver) {
      return watcher->listAgain();
    }
  }
  return std::unexpected(std::string(driver) + ": not watched");
}

std::expected<bitoscope::host::OpenedDevice, bitoscope::host::OpenRefusal> PipeDeviceHost::open(
    std::string_view driver, std::string_view deviceId) {
  host_.close(std::string(driver) + "/" + std::string(deviceId));
  auto id = host_.open(driver, deviceId);
  if (!id) {
    return std::unexpected(std::move(id.error()));
  }
  const auto connection = host_.connection(*id);
  if (connection == nullptr) {
    return std::unexpected(bitoscope::host::OpenRefusal{.message = "driver \"" + *id + "\" was opened but is gone"});
  }
  auto device = connection->device();
  if (!device) {
    return std::unexpected(bitoscope::host::OpenRefusal{.message = "driver \"" + *id + "\" made no handshake"});
  }
  return bitoscope::host::OpenedDevice{std::move(*device), connection->pluginVersion()};
}

void PipeDeviceHost::stopWatching() {
  std::vector<std::pair<std::string, std::unique_ptr<bitoscope::host::DeviceWatcher>>> watchers;
  {
    std::lock_guard lock(mutex_);
    watchers.swap(watchers_);
  }
  for (auto& [name, watcher] : watchers) {
    watcher->stop();
  }
}

}
