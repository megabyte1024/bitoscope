/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <string>

namespace bitoscope::gui_session {

struct DeviceIdentity {
  std::string driver;
  std::string id;
  std::string serialNumber;

  bool operator==(const DeviceIdentity&) const = default;

  bool sameDevice(const DeviceIdentity& reappeared) const;
};

std::optional<std::size_t> findReplugged(const DeviceIdentity& lost,
                                         std::span<const DeviceIdentity> present);

}
