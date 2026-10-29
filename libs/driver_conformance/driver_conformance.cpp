/*
 * SPDX-License-Identifier: MIT
 */

#include "driver_conformance.h"

#include <algorithm>
#include <array>
#include <condition_variable>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <utility>

#include "driver_runs.h"
#include "ipc_messages.h"
#include "plugin_version.h"

namespace bitoscope::driver_conformance {

namespace {

constexpr std::array kRules = {
    Rule{"interface", true, "--interface names usb, serial, network or none, and exits 0"},
    Rule{"list", true, "--list writes one device list and exits 0, both times"},
    Rule{"list-same-ids", true, "both lists and watch's first list give same ids"},
    Rule{"ids-unique", true, "no list names one id twice"},
    Rule{"plugin-version", true, "every device list and accepted handshake carries valid plugin version"},
    Rule{"reason-codes", true, "every listed and reported entry carries known reason code"},
    Rule{"unavailable-entries", true,
         "unavailable entry that is not identified carries no facets; reason other carries detail"},
    Rule{"watch", true, "--watch starts with device list that says it watches, and keeps running"},
    Rule{"watch-list-again", true, "after list request, watch reports no unchanged entry again"},
    Rule{"handshake", true, "handshake for each available listed device is accepted with entry of that id"},
    Rule{"clean-exit", true,
         "--watch and --device processes end when host closes their input, with code 0 after accepted handshake"},
    Rule{"open-progress", false, "open progress arrives before handshake reply"},
    Rule{"busy-refusal", false, "handshake for device listed busy is refused with busy code"},
    Rule{"open-codes", false, "every open progress code and handshake refusal code is known"},
};

constexpr auto kLastReasonCode = static_cast<uint16_t>(ipc::UnavailableReason::other);
constexpr auto kLastProgressCode = static_cast<uint16_t>(ipc::OpenProgressCode::waitingForDevice);

class Report {
 public:
  void fail(std::string_view rule, std::string problem) { problems_[std::string(rule)].push_back(std::move(problem)); }
  void shown(std::string_view rule) { shown_.insert(std::string(rule)); }

  std::vector<RuleResult> results() const {
    std::vector<RuleResult> out;
    for (const Rule& rule : kRules) {
      RuleResult result{std::string(rule.id), rule.required, Verdict::pass, {}, {}};
      if (const auto found = problems_.find(result.rule); found != problems_.end()) {
        result.verdict = Verdict::fail;
        result.problems = found->second;
      } else if (!rule.required && !shown_.contains(result.rule)) {
        result.verdict = Verdict::skip;
      }
      out.push_back(std::move(result));
    }
    return out;
  }

 private:
  std::map<std::string, std::vector<std::string>> problems_;
  std::set<std::string> shown_;
};

std::string inQuotes(std::string_view text) { return "\"" + std::string(text) + "\""; }

std::string idSet(const std::set<std::string>& ids) {
  std::string text = "{";
  for (const std::string& id : ids) text += (text.size() > 1 ? ", " : "") + inQuotes(id);
  return text + "}";
}

std::set<std::string> idsOf(const std::vector<ipc::DeviceInfo>& devices) {
  std::set<std::string> ids;
  for (const ipc::DeviceInfo& device : devices) ids.insert(device.id);
  return ids;
}

void checkVersion(Report& report, const std::string& where, std::string_view version) {
  if (auto parsed = ipc::parsePluginVersion(version); !parsed) {
    report.fail("plugin-version", where + " carries plugin version " + inQuotes(version) + ": " + parsed.error());
  }
}

void checkEntry(Report& report, const std::string& where, const ipc::DeviceInfo& device) {
  const auto code = static_cast<uint16_t>(device.unavailable);
  if (code > kLastReasonCode) {
    report.fail("reason-codes", where + " gives " + inQuotes(device.id) + " unknown reason code " + std::to_string(code));
  }
  if (device.unavailable == ipc::UnavailableReason::none) return;
  if (!device.identified && !device.facets.empty()) {
    report.fail("unavailable-entries", where + " lists " + inQuotes(device.id) + " unavailable and not identified, with " +
                                           std::to_string(device.facets.size()) + " facet(s)");
  }
  if (device.unavailable == ipc::UnavailableReason::other && device.unavailableDetail.empty()) {
    report.fail("unavailable-entries",
                where + " lists " + inQuotes(device.id) + " unavailable for reason other, without detail");
  }
}

void checkIdsUnique(Report& report, const std::string& where, const std::vector<ipc::DeviceInfo>& devices) {
  std::map<std::string, int> counts;
  for (const ipc::DeviceInfo& device : devices) ++counts[device.id];
  for (const auto& [id, count] : counts) {
    if (count > 1) report.fail("ids-unique", where + " names " + inQuotes(id) + " " + std::to_string(count) + " times");
  }
}

void checkList(Report& report, const std::string& where, const ipc::DeviceList& list) {
  checkIdsUnique(report, where, list.devices);
  checkVersion(report, where, list.pluginVersion);
  for (const ipc::DeviceInfo& device : list.devices) checkEntry(report, where, device);
}

void checkInterface(Report& report, const ConformanceOptions& options) {
  const InterfaceRun run = runInterface(options);
  for (const std::string& problem : run.problems) report.fail("interface", problem);
  static constexpr std::array kKinds = {"usb", "serial", "network", "none"};
  if (std::ranges::find(kKinds, run.output) == kKinds.end()) {
    report.fail("interface", "--interface answered " + inQuotes(run.output));
  }
  if (run.exitCode && *run.exitCode != 0) {
    report.fail("interface", "--interface exited with code " + std::to_string(*run.exitCode));
  }
}

std::optional<ipc::DeviceList> checkedList(Report& report, const ConformanceOptions& options, const std::string& label) {
  ListRun run = runList(options, label);
  for (const std::string& problem : run.problems) report.fail("list", problem);
  if (run.list) checkList(report, label, *run.list);
  return std::move(run.list);
}

struct WatchState {
  std::mutex mutex;
  std::condition_variable cv;
  bool firstListDone = false;
  bool closing = false;
  std::map<std::string, ipc::DeviceInfo> entries;
  std::vector<ipc::DeviceInfo> firstList;
  std::vector<ipc::DeviceInfo> reports;
  std::vector<std::string> unchanged;
  std::optional<host::WatchEnd> ended;
};

host::DeviceWatcherEvents watchEvents(const std::shared_ptr<WatchState>& state) {
  host::DeviceWatcherEvents events;
  events.onDeviceAppeared = [state](const ipc::DeviceInfo& device) {
    std::lock_guard lock(state->mutex);
    (state->firstListDone ? state->reports : state->firstList).push_back(device);
    state->entries[device.id] = device;
  };
  events.onDeviceChanged = [state](const ipc::DeviceInfo& device) {
    std::lock_guard lock(state->mutex);
    state->reports.push_back(device);
    if (const auto known = state->entries.find(device.id); known != state->entries.end() && known->second == device) {
      state->unchanged.push_back(device.id);
    }
    state->entries[device.id] = device;
  };
  events.onDeviceLost = [state](std::string_view id, std::string_view) {
    std::lock_guard lock(state->mutex);
    state->entries.erase(std::string(id));
  };
  events.onEnded = [state](const host::WatchEnd& end) {
    {
      std::lock_guard lock(state->mutex);
      if (state->closing) return;
      state->ended = end;
    }
    state->cv.notify_all();
  };
  return events;
}

std::optional<std::set<std::string>> checkWatch(Report& report, const ConformanceOptions& options) {
  const std::string who = "--watch";
  const auto notStarted = [&](const std::string& why) {
    report.fail("watch", why);
    report.fail("watch-list-again", "not checked: --watch did not start");
    return std::nullopt;
  };
  auto process = host::PluginProcess::start(host::PluginProcessOptions{
      options.driver, modeArguments(who, options.connection), who, options.onDriverStderr});
  if (!process) return notStarted(who + " could not start: " + process.error());
  host::PluginProcess* const driver = process->get();

  auto state = std::make_shared<WatchState>();
  auto watcher = host::DeviceWatcher::start(std::unique_ptr<host::PluginLink>(std::move(*process)), watchEvents(state),
                                            options.timeout);
  if (!watcher) return notStarted(watcher.error());
  {
    std::lock_guard lock(state->mutex);
    state->firstListDone = true;
  }
  if (auto asked = (*watcher)->listAgain(); !asked) report.fail("watch-list-again", asked.error());
  std::vector<ipc::DeviceInfo> firstList;
  std::vector<ipc::DeviceInfo> reports;
  std::vector<std::string> unchanged;
  std::optional<host::WatchEnd> ended;
  {
    std::unique_lock lock(state->mutex);
    state->cv.wait_for(lock, options.listAgainWait, [&] { return state->ended.has_value(); });
    state->closing = true;
    firstList = state->firstList;
    reports = state->reports;
    unchanged = state->unchanged;
    ended = state->ended;
  }

  const std::string firstWhere = who + "'s first list";
  checkIdsUnique(report, firstWhere, firstList);
  for (const ipc::DeviceInfo& device : firstList) checkEntry(report, firstWhere, device);
  for (const ipc::DeviceInfo& device : reports) checkEntry(report, who + " report", device);
  for (const std::string& id : unchanged) {
    report.fail("watch-list-again", who + " reported " + inQuotes(id) + " again with unchanged entry");
  }
  if (ended) {
    report.fail("watch", who + " ended before host closed its input: " + ended->message);
  } else {
    driver->closeInput();
    const std::optional<int> code = driver->waitForExit(options.exitWait);
    if (!code) {
      report.fail("clean-exit", who + " did not end within " + std::to_string(options.exitWait.count()) +
                                    " ms after its input closed");
    } else if (*code != 0) {
      report.fail("clean-exit", who + " exited with code " + std::to_string(*code) + " after its input closed");
    }
  }
  (*watcher)->stop();
  return idsOf(firstList);
}

void checkDevice(Report& report, const ConformanceOptions& options, const ipc::DeviceInfo& listed) {
  const bool available = listed.unavailable == ipc::UnavailableReason::none;
  const bool busy = listed.unavailable == ipc::UnavailableReason::busy;
  if (!available && !busy) return;
  const std::string who = "--device " + listed.id;
  const DeviceRun run = runDevice(options, listed.id);
  for (const std::string& problem : run.problems) report.fail("handshake", problem);
  if (run.exitProblem) report.fail("clean-exit", *run.exitProblem);

  if (!run.progressBeforeReply.empty() || !run.progressAfterReply.empty()) {
    report.shown("open-progress");
    report.shown("open-codes");
  }
  if (!run.progressAfterReply.empty()) {
    report.fail("open-progress", who + " sent " + std::to_string(run.progressAfterReply.size()) +
                                     " open progress report(s) after its handshake reply");
  }
  for (const auto* reports : {&run.progressBeforeReply, &run.progressAfterReply}) {
    for (const ipc::OpenProgress& progress : *reports) {
      const auto code = static_cast<uint16_t>(progress.code);
      if (code > kLastProgressCode) {
        report.fail("open-codes", who + " sent open progress with unknown code " + std::to_string(code));
      }
    }
  }

  if (!run.reply) return;
  const ipc::HandshakeReply& reply = *run.reply;
  if (reply.accepted) {
    if (reply.device.id != listed.id) {
      report.fail("handshake", who + " accepted handshake with entry of id " + inQuotes(reply.device.id));
    }
    if (reply.device.unavailable != ipc::UnavailableReason::none) {
      report.fail("handshake", who + " accepted handshake with entry that says unavailable");
    }
    checkVersion(report, who + "'s handshake reply", reply.pluginVersion);
    return;
  }
  report.shown("open-codes");
  const auto code = static_cast<uint16_t>(reply.unavailable);
  if (code > kLastReasonCode) {
    report.fail("open-codes", who + " refused handshake with unknown reason code " + std::to_string(code));
  }
  if (available) report.fail("handshake", who + " refused handshake: " + reply.message);
  if (busy) {
    report.shown("busy-refusal");
    if (reply.unavailable != ipc::UnavailableReason::busy) {
      report.fail("busy-refusal", who + " refused handshake with reason code " + std::to_string(code) +
                                      ", not busy (" +
                                      std::to_string(static_cast<uint16_t>(ipc::UnavailableReason::busy)) + ")");
    }
  }
}

void checkSameIds(Report& report, const std::set<std::string>& first, const std::string& where,
                  const std::set<std::string>& other) {
  if (other != first) {
    report.fail("list-same-ids", where + " gives ids " + idSet(other) + ", first --list gives " + idSet(first));
  }
}

}

std::span<const Rule> rules() { return kRules; }

std::vector<RuleResult> runConformance(const ConformanceOptions& options) {
  Report report;
  checkInterface(report, options);
  const std::optional<ipc::DeviceList> first = checkedList(report, options, "first --list");
  const std::optional<ipc::DeviceList> second = checkedList(report, options, "second --list");
  const std::optional<std::set<std::string>> watched = checkWatch(report, options);

  if (!first) {
    report.fail("list-same-ids", "not checked: first --list gave no device list");
    report.fail("handshake", "not checked: first --list gave no device list");
    return report.results();
  }
  const std::set<std::string> firstIds = idsOf(first->devices);
  if (second) {
    checkSameIds(report, firstIds, "second --list", idsOf(second->devices));
  } else {
    report.fail("list-same-ids", "not checked: second --list gave no device list");
  }
  if (watched) checkSameIds(report, firstIds, "--watch's first list", *watched);

  std::set<std::string> opened;
  for (const ipc::DeviceInfo& device : first->devices) {
    if (opened.insert(device.id).second) checkDevice(report, options, device);
  }
  return report.results();
}

bool conforms(std::span<const RuleResult> results) {
  return std::ranges::none_of(results, [](const RuleResult& r) { return r.verdict == Verdict::fail; });
}

std::string resultLine(const RuleResult& result, std::span<const Rule> table) {
  std::string_view description;
  for (const Rule& rule : table) {
    if (rule.id == result.rule) description = rule.description;
  }
  const std::string name = result.rule + (result.required ? "" : " (optional)");
  switch (result.verdict) {
    case Verdict::pass:
      return "PASS " + name + ": " + std::string(description);
    case Verdict::skip:
      return "SKIP " + name + ": " + std::string(description) + ": " +
             (result.skipReason.empty() ? std::string("not shown by driver") : result.skipReason);
    case Verdict::fail:
      break;
  }
  std::string line = "FAIL " + name + ": " + std::string(description);
  for (const std::string& problem : result.problems) line += "\n    " + problem;
  return line;
}

std::string summaryLine(std::span<const RuleResult> results) {
  const auto count = [&](Verdict verdict) {
    return std::to_string(std::ranges::count(results, verdict, &RuleResult::verdict));
  };
  return std::string(conforms(results) ? "conforms: " : "does not conform: ") + count(Verdict::pass) + " pass, " +
         count(Verdict::fail) + " fail, " + count(Verdict::skip) + " skipped";
}

}
