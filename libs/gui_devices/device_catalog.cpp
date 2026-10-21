/*
 * SPDX-License-Identifier: MIT
 */

#include "device_catalog.h"

#include <algorithm>
#include <utility>

namespace bitoscope::gui_devices {

void DeviceCatalog::driversFound(std::filesystem::path directory, std::vector<std::string> drivers) {
  directory_ = std::move(directory);
  drivers_.clear();
  for (std::string& name : drivers) {
    DriverEntry entry;
    entry.name = std::move(name);
    drivers_.push_back(std::move(entry));
  }
  driversKnown_ = true;
}

const DriverEntry* DeviceCatalog::driver(std::string_view name) const {
  const auto it = std::ranges::find(drivers_, name, &DriverEntry::name);
  return it == drivers_.end() ? nullptr : &*it;
}

DriverEntry* DeviceCatalog::mutableDriver(std::string_view name) {
  const auto it = std::ranges::find(drivers_, name, &DriverEntry::name);
  return it == drivers_.end() ? nullptr : &*it;
}

void DeviceCatalog::listing(std::string_view driver) {
  if (DriverEntry* entry = mutableDriver(driver)) {
    entry->state = ListingState::Listing;
  }
}

void DeviceCatalog::listed(std::string_view driver,
                           const std::expected<bitoscope::ipc::DeviceList, std::string>& list) {
  DriverEntry* entry = mutableDriver(driver);
  if (entry == nullptr) {
    return;
  }
  const bool first = !entry->answered;
  entry->answered = true;
  if (!list) {
    entry->state = ListingState::Failed;
    entry->problem = list.error();
    entry->devices.clear();
    return;
  }
  entry->state = ListingState::Listed;
  if (!list->driverName.empty()) {
    entry->displayName = list->driverName;
  }
  entry->pluginVersion = list->pluginVersion;
  entry->problem.clear();
  entry->devices = list->devices;
  entry->supportsWatch = list->supportsWatch;
  if (first && list->supportsWatch) {
    entry->watching = true;
  }
}

void DeviceCatalog::deviceAppeared(std::string_view driver, const bitoscope::ipc::DeviceInfo& device) {
  DriverEntry* entry = mutableDriver(driver);
  if (entry == nullptr) {
    return;
  }
  const auto it = std::ranges::find(entry->devices, device.id, &bitoscope::ipc::DeviceInfo::id);
  if (it != entry->devices.end()) {
    *it = device;
  } else {
    entry->devices.push_back(device);
  }
}

void DeviceCatalog::deviceLost(std::string_view driver, std::string_view deviceId) {
  if (DriverEntry* entry = mutableDriver(driver)) {
    std::erase_if(entry->devices,
                  [deviceId](const bitoscope::ipc::DeviceInfo& d) { return d.id == deviceId; });
  }
}

void DeviceCatalog::watchEnded(std::string_view driver, std::string_view reason) {
  if (DriverEntry* entry = mutableDriver(driver)) {
    entry->watching = false;
    entry->watchProblem = std::string(reason);
  }
}

const bitoscope::ipc::DeviceInfo* DeviceCatalog::find(std::string_view driver,
                                                      std::string_view deviceId) const {
  const DriverEntry* entry = this->driver(driver);
  if (entry == nullptr) {
    return nullptr;
  }
  const auto it = std::ranges::find(entry->devices, deviceId, &bitoscope::ipc::DeviceInfo::id);
  return it == entry->devices.end() ? nullptr : &*it;
}

std::vector<bitoscope::gui_wizard::WizardDriver> DeviceCatalog::wizardDrivers() const {
  std::vector<bitoscope::gui_wizard::WizardDriver> out;
  out.reserve(drivers_.size());
  for (const DriverEntry& entry : drivers_) {
    bitoscope::gui_wizard::WizardDriver driver;
    driver.search = bitoscope::gui_wizard::DeviceSearch::DeviceList;
    for (const auto& device : entry.devices) {
      driver.devices.push_back(device.id);
      if (device.unavailable != bitoscope::ipc::UnavailableReason::none) {
        driver.unavailable.push_back(device.id);
      }
    }
    out.push_back(std::move(driver));
  }
  return out;
}

std::vector<std::string> DeviceCatalog::driversToRelist() const {
  std::vector<std::string> out;
  for (const DriverEntry& entry : drivers_) {
    if (entry.state != ListingState::Listing && !entry.watching) {
      out.push_back(entry.name);
    }
  }
  return out;
}

}
