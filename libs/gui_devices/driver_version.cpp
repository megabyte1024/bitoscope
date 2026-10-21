/*
 * SPDX-License-Identifier: MIT
 */

#include "driver_version.h"

namespace bitoscope::gui_devices {

std::string listedDriverVersion(const DeviceCatalog& catalog, std::string_view driver) {
  const DriverEntry* entry = catalog.driver(driver);
  return entry != nullptr ? entry->pluginVersion : std::string();
}

std::string sessionDriverVersion(const DeviceRouter& router, const bitoscope::gui_session::DeviceIdentity& identity) {
  const bitoscope::host::OpenedDevice* opened = router.openedFor(identity);
  if (opened != nullptr && !opened->pluginVersion.empty()) {
    return opened->pluginVersion;
  }
  return listedDriverVersion(router.catalog(), identity.driver);
}

std::string pickerDriverVersion(const DeviceRouter& router, std::string_view driver,
                                const bitoscope::ipc::DeviceInfo& listed) {
  if (router.isOpen(driver, listed.id)) {
    const bitoscope::host::OpenedDevice* opened = router.opened(driver, listed.id);
    if (opened != nullptr && !opened->pluginVersion.empty()) {
      return opened->pluginVersion;
    }
  }
  return listedDriverVersion(router.catalog(), driver);
}

std::vector<DriverVersionRow> driverVersionRows(const DeviceCatalog& catalog) {
  std::vector<DriverVersionRow> rows;
  rows.reserve(catalog.drivers().size());
  for (const DriverEntry& entry : catalog.drivers()) {
    rows.push_back({entry.shownName(), entry.pluginVersion});
  }
  return rows;
}

}
