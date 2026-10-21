/*
 * SPDX-License-Identifier: MIT
 */

#include "list_again.h"

#include <algorithm>

namespace bitoscope::gui_devices {

namespace gs = bitoscope::gui_session;

namespace {

const std::set<gs::SessionId>& waitingOn(const std::map<std::string, std::set<gs::SessionId>>& waiting,
                                          const std::string& driver) {
  static const std::set<gs::SessionId> kNone;
  const auto it = waiting.find(driver);
  return it == waiting.end() ? kNone : it->second;
}

}

ListAgainStep listAgainStep(const ListAgainMemory& before, const ListAgainInputs& now, std::uint64_t nowMs) {
  ListAgainStep step;
  step.memory.pickerOpen = now.pickerOpen;
  step.memory.waiting = now.waiting;
  for (const std::string& driver : now.watching) {
    const std::set<gs::SessionId>& waiting = waitingOn(now.waiting, driver);
    if (!now.pickerOpen && waiting.empty()) {
      continue;
    }
    const std::set<gs::SessionId>& waitedBefore = waitingOn(before.waiting, driver);
    const bool startsWaiting =
      std::ranges::any_of(waiting, [&](gs::SessionId id) { return !waitedBefore.contains(id); });
    const auto last = before.lastAskedMs.find(driver);
    const bool atOnce = last == before.lastAskedMs.end() || (now.pickerOpen && !before.pickerOpen) || startsWaiting;
    const bool due = !atOnce && nowMs >= last->second && nowMs - last->second >= kListAgainPeriodMs;
    if (atOnce || due) {
      step.requests.push_back(ListAgainRequest{.driver = driver, .periodic = due});
      step.memory.lastAskedMs[driver] = nowMs;
    } else {
      step.memory.lastAskedMs[driver] = last->second;
    }
  }
  return step;
}

ListAgainInputs listAgainInputs(const DeviceCatalog& catalog, const gs::SessionList& sessions, bool pickerOpen) {
  ListAgainInputs inputs;
  inputs.pickerOpen = pickerOpen;
  for (const DriverEntry& entry : catalog.drivers()) {
    if (entry.watching) {
      inputs.watching.push_back(entry.name);
    }
  }
  for (const gs::Session& session : sessions.sessions()) {
    if (session.device.has_value() && session.waitsForDevice()) {
      inputs.waiting[session.device->driver].insert(session.id);
    }
  }
  return inputs;
}

}
