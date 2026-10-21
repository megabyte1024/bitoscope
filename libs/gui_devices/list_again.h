/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "device_catalog.h"
#include "session_list.h"

namespace bitoscope::gui_devices {

inline constexpr std::uint64_t kListAgainPeriodMs = 2000;

struct ListAgainInputs {
  bool pickerOpen = false;
  std::vector<std::string> watching;
  std::map<std::string, std::set<bitoscope::gui_session::SessionId>> waiting;
};

struct ListAgainMemory {
  bool pickerOpen = false;
  std::map<std::string, std::set<bitoscope::gui_session::SessionId>> waiting;
  std::map<std::string, std::uint64_t> lastAskedMs;
};

struct ListAgainRequest {
  std::string driver;
  bool periodic = false;
  bool operator==(const ListAgainRequest&) const = default;
};

struct ListAgainStep {
  std::vector<ListAgainRequest> requests;
  ListAgainMemory memory;
};

ListAgainStep listAgainStep(const ListAgainMemory& before, const ListAgainInputs& now, std::uint64_t nowMs);

ListAgainInputs listAgainInputs(const DeviceCatalog& catalog, const bitoscope::gui_session::SessionList& sessions,
                                bool pickerOpen);

}
