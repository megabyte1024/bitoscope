/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "device_catalog.h"
#include "device_identity.h"
#include "device_router.h"
#include "ipc_messages.h"

namespace bitoscope::gui_devices {

struct DriverVersionRow {
  std::string name;
  std::string version;
  bool operator==(const DriverVersionRow&) const = default;
};

std::string listedDriverVersion(const DeviceCatalog& catalog, std::string_view driver);

std::string sessionDriverVersion(const DeviceRouter& router, const bitoscope::gui_session::DeviceIdentity& identity);

std::string pickerDriverVersion(const DeviceRouter& router, std::string_view driver,
                                const bitoscope::ipc::DeviceInfo& listed);

std::vector<DriverVersionRow> driverVersionRows(const DeviceCatalog& catalog);

}
