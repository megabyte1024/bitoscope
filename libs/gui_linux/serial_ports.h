/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <optional>
#include <span>
#include <string>
#include <vector>

namespace bitoscope::gui_linux {

struct TtyEntry {
  std::string name;
  bool hasDevice = false;
  std::optional<std::string> uartType;
  bool deviceNodeExists = false;
};

std::vector<std::string> serialPortsFromSysfs(std::span<const TtyEntry> entries);

}
