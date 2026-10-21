/*
 * SPDX-License-Identifier: MIT
 */

#include "device_label.h"

#include <algorithm>
#include <format>

namespace bitoscope::gui_devices {

std::string shownDriverName(const DeviceCatalog& catalog, std::string_view driver) {
  const DriverEntry* entry = catalog.driver(driver);
  return entry != nullptr ? entry->shownName() : std::string(driver);
}

std::string deviceLabel(std::string_view driver, const bitoscope::ipc::DeviceInfo& device) {
  return std::format("{} ({}/{})", device.name.empty() ? device.id : device.name, driver, device.id);
}

std::string deviceListLabel(const bitoscope::ipc::DeviceInfo& device) {
  return device.name.empty() ? device.id : std::format("{} ({})", device.name, device.id);
}

namespace {

const bitoscope::ipc::DeviceInfo* listedFor(const DeviceCatalog& catalog,
                                            const bitoscope::gui_session::DeviceIdentity& identity) {
  const DriverEntry* entry = catalog.driver(identity.driver);
  if (entry == nullptr) {
    return nullptr;
  }
  const auto it = std::ranges::find_if(entry->devices, [&](const bitoscope::ipc::DeviceInfo& device) {
    return identity.sameDevice(identityOf(identity.driver, device));
  });
  return it == entry->devices.end() ? nullptr : &*it;
}

}

std::optional<std::string> openedDeviceLabel(const DeviceRouter& router,
                                             const bitoscope::gui_session::DeviceIdentity& identity) {
  const std::string driver = shownDriverName(router.catalog(), identity.driver);
  const bitoscope::ipc::DeviceInfo* opened = router.openedDeviceFor(identity);
  if (opened != nullptr && !opened->name.empty()) {
    return deviceLabel(driver, *opened);
  }
  const bitoscope::ipc::DeviceInfo* listed = listedFor(router.catalog(), identity);
  if (listed != nullptr && listed->identified && !listed->name.empty()) {
    return deviceLabel(driver, *listed);
  }
  return std::nullopt;
}

std::string pickerDeviceLabel(const DeviceRouter& router, std::string_view driver,
                              const bitoscope::ipc::DeviceInfo& listed) {
  if (router.isOpen(driver, listed.id)) {
    const bitoscope::ipc::DeviceInfo* opened = router.openedDevice(driver, listed.id);
    if (opened != nullptr && !opened->name.empty()) {
      return deviceListLabel(*opened);
    }
  }
  return deviceListLabel(listed);
}

}
