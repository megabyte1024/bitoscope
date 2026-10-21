/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <string>

#include "device_catalog.h"

namespace bitoscope::gui_devices {

enum class WatchMark {
  None,
  Watched,
  NotWatched,
};

struct DriverWatchMark {
  WatchMark mark = WatchMark::None;
  std::string reason;

  bool operator==(const DriverWatchMark&) const = default;
};

DriverWatchMark driverWatchMark(const DriverEntry& entry);

}
