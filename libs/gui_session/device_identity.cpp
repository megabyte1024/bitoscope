/*
 * SPDX-License-Identifier: MIT
 */

#include "device_identity.h"

namespace bitoscope::gui_session {

bool DeviceIdentity::sameDevice(const DeviceIdentity& reappeared) const {
  if (!serialNumber.empty()) {
    return reappeared.serialNumber == serialNumber;
  }
  return reappeared.driver == driver && reappeared.id == id;
}

std::optional<std::size_t> findReplugged(const DeviceIdentity& lost,
                                         std::span<const DeviceIdentity> present) {
  for (std::size_t i = 0; i < present.size(); ++i) {
    if (lost.sameDevice(present[i])) {
      return i;
    }
  }
  if (!lost.serialNumber.empty()) {
    return std::nullopt;
  }
  std::optional<std::size_t> only;
  for (std::size_t i = 0; i < present.size(); ++i) {
    if (present[i].driver != lost.driver) {
      continue;
    }
    if (only.has_value()) {
      return std::nullopt;
    }
    only = i;
  }
  return only;
}

}
