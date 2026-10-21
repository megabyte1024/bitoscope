/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "device_catalog.h"
#include "device_identity.h"
#include "device_router.h"
#include "ipc_messages.h"

namespace bitoscope::gui_devices {

std::string shownDriverName(const DeviceCatalog& catalog, std::string_view driver);

std::string deviceLabel(std::string_view driver, const bitoscope::ipc::DeviceInfo& device);

std::string deviceListLabel(const bitoscope::ipc::DeviceInfo& device);

std::optional<std::string> openedDeviceLabel(const DeviceRouter& router,
                                             const bitoscope::gui_session::DeviceIdentity& identity);

std::string pickerDeviceLabel(const DeviceRouter& router, std::string_view driver,
                              const bitoscope::ipc::DeviceInfo& listed);

}
