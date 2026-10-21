/*
 * SPDX-License-Identifier: MIT
 */

#include "device_router.h"

#include <algorithm>
#include <format>

namespace bitoscope::gui_devices {

namespace gs = bitoscope::gui_session;

namespace {

gs::DeviceUnavailable unavailableKind(bitoscope::ipc::UnavailableReason why) {
  switch (why) {
    case bitoscope::ipc::UnavailableReason::none:
      return gs::DeviceUnavailable::None;
    case bitoscope::ipc::UnavailableReason::busy:
      return gs::DeviceUnavailable::Busy;
    case bitoscope::ipc::UnavailableReason::noPermission:
      return gs::DeviceUnavailable::NoPermission;
    case bitoscope::ipc::UnavailableReason::firmwareLoading:
      return gs::DeviceUnavailable::FirmwareLoading;
    case bitoscope::ipc::UnavailableReason::other:
      return gs::DeviceUnavailable::Other;
  }
  return gs::DeviceUnavailable::Other;
}

std::string unavailableWords(const bitoscope::ipc::DeviceInfo& device) {
  switch (device.unavailable) {
    case bitoscope::ipc::UnavailableReason::none:
      return {};
    case bitoscope::ipc::UnavailableReason::busy:
      return "in use by another program";
    case bitoscope::ipc::UnavailableReason::noPermission:
      return "no permission";
    case bitoscope::ipc::UnavailableReason::firmwareLoading:
      return "firmware loading";
    case bitoscope::ipc::UnavailableReason::other:
      return device.unavailableDetail.empty() ? std::string("driver gave no reason") : device.unavailableDetail;
  }
  return std::format("unknown reason (code {})", static_cast<unsigned>(device.unavailable));
}

std::string progressWords(const bitoscope::ipc::OpenProgress& progress) {
  std::string words;
  switch (progress.code) {
    case bitoscope::ipc::OpenProgressCode::firmwareLoading:
      words = "firmware loading";
      break;
    case bitoscope::ipc::OpenProgressCode::waitingForDevice:
      words = "waiting for device to come back";
      break;
    case bitoscope::ipc::OpenProgressCode::other:
      break;
  }
  if (words.empty()) {
    return progress.detail.empty() ? std::string("in progress") : progress.detail;
  }
  return progress.detail.empty() ? words : std::format("{} ({})", words, progress.detail);
}

bool waitedFor(gs::DeviceUnavailable kind) {
  return kind == gs::DeviceUnavailable::Busy || kind == gs::DeviceUnavailable::FirmwareLoading;
}

}

gs::DeviceIdentity identityOf(std::string_view driver, const bitoscope::ipc::DeviceInfo& device) {
  return gs::DeviceIdentity{
      .driver = std::string(driver), .id = device.id, .serialNumber = device.serialNumber};
}

std::optional<std::pair<std::string, std::string>> splitPluginId(std::string_view pluginId) {
  const std::size_t slash = pluginId.find('/');
  if (slash == std::string_view::npos || slash == 0 || slash + 1 == pluginId.size()) {
    return std::nullopt;
  }
  return std::pair<std::string, std::string>{std::string(pluginId.substr(0, slash)),
                                             std::string(pluginId.substr(slash + 1))};
}

DeviceRouter::DeviceRouter(gs::SessionList& sessions) : sessions_(sessions) {}

void DeviceRouter::requestOpen(const Key& key, bool replug, RouterOutput& out) {
  if (replug) {
    replug_.insert(key);
  }
  if (pending_.insert(key).second) {
    if (plugins_.contains(key)) {
      closing_.insert(key);
    }
    out.open.push_back(DeviceRequest{key.first, key.second});
    out.log.push_back(std::format("Opening {}/{}", key.first, key.second));
  }
}

bool DeviceRouter::heldHere(const Key& key) const {
  return open_.contains(key) || pending_.contains(key) || plugins_.contains(key);
}

bool DeviceRouter::watching(std::string_view driver) const {
  const DriverEntry* entry = catalog_.driver(driver);
  return entry != nullptr && entry->watching;
}

bool DeviceRouter::pendingFor(const gs::Session& session) const {
  return session.device.has_value() &&
         pending_.contains(Key{session.device->driver, session.device->id});
}

RouterOutput DeviceRouter::bindSession(gs::SessionId session, std::string_view driver,
                                       const bitoscope::ipc::DeviceInfo& device) {
  RouterOutput out;
  sessions_.setDevice(session, identityOf(driver, device));
  const Key key{std::string(driver), device.id};
  if (open_.contains(key)) {
    return out;
  }
  if (device.unavailable != bitoscope::ipc::UnavailableReason::none && !heldHere(key) && watching(driver)) {
    sessions_.deviceUnavailable(driver, device.id, unavailableKind(device.unavailable), unavailableWords(device));
    return out;
  }
  sessions_.deviceConnecting(driver, device.id);
  requestOpen(key, false, out);
  return out;
}

RouterOutput DeviceRouter::restoreSession(gs::SessionId session, const gs::DeviceIdentity& identity) {
  RouterOutput out;
  sessions_.setDevice(session, identity);
  sessions_.deviceConnecting(identity.driver, identity.id);
  if (!catalog_.driversKnown()) {
    return out;
  }
  const DriverEntry* entry = catalog_.driver(identity.driver);
  if (entry == nullptr) {
    sessions_.deviceLost(identity.driver, identity.id,
                         std::format("no driver \"{}\" in {}", identity.driver,
                                     catalog_.directory().string()));
  } else if (entry->state != ListingState::Listing) {
    settleDriver(identity.driver, out);
  }
  return out;
}

RouterOutput DeviceRouter::driversFound(const std::filesystem::path& directory,
                                        const std::vector<std::string>& drivers) {
  RouterOutput out;
  catalog_.driversFound(directory, drivers);
  if (drivers.empty()) {
    out.log.push_back(std::format("No driver found in {}", directory.string()));
  } else {
    std::string names;
    for (const std::string& name : drivers) {
      names += (names.empty() ? "" : ", ") + name;
    }
    out.log.push_back(std::format("Drivers in {}: {}", directory.string(), names));
  }
  for (const gs::Session& session : std::vector<gs::Session>(sessions_.sessions())) {
    if (session.device.has_value() && session.deviceState == gs::DeviceState::Connecting &&
        !pendingFor(session) && std::ranges::find(drivers, session.device->driver) == drivers.end()) {
      sessions_.deviceLost(session.device->driver, session.device->id,
                           std::format("no driver \"{}\" in {}", session.device->driver,
                                       directory.string()));
    }
  }
  return out;
}

RouterOutput DeviceRouter::devicesListed(
    std::string_view driver, const std::expected<bitoscope::ipc::DeviceList, std::string>& list) {
  RouterOutput out;
  catalog_.listed(driver, list);
  if (!list) {
    out.log.push_back(std::format("{}: could not list its devices: {}", driver, list.error()));
  } else {
    out.log.push_back(std::format("{}: {} device(s){}", driver, list->devices.size(),
                                  list->supportsWatch ? ", plug-and-play" : ""));
  }
  settleDriver(driver, out);
  return out;
}

std::vector<gs::DeviceIdentity> DeviceRouter::present() const {
  std::vector<gs::DeviceIdentity> out;
  for (const DriverEntry& entry : catalog_.drivers()) {
    for (const auto& device : entry.devices) {
      out.push_back(identityOf(entry.name, device));
    }
  }
  return out;
}

bool DeviceRouter::isFor(const gs::Session& session, const gs::DeviceIdentity& identity,
                         std::span<const gs::DeviceIdentity> present) {
  if (!session.device.has_value()) {
    return false;
  }
  const auto found = gs::findReplugged(*session.device, present);
  return found.has_value() && present[*found] == identity;
}

void DeviceRouter::offerDevice(std::string_view driver, const bitoscope::ipc::DeviceInfo& device,
                               RouterOutput& out, std::set<gs::SessionId>* matched) {
  const gs::DeviceIdentity identity = identityOf(driver, device);
  const Key key{std::string(driver), device.id};
  std::vector<gs::DeviceIdentity> now = present();
  if (std::ranges::find(now, identity) == now.end()) {
    now.push_back(identity);
  }
  bool wanted = false;
  bool replug = false;
  std::vector<gs::DeviceIdentity> waiting;
  for (const gs::Session& session : sessions_.sessions()) {
    if (session.deviceState == gs::DeviceState::Connected || !isFor(session, identity, now)) {
      continue;
    }
    if (matched != nullptr) {
      matched->insert(session.id);
    }
    if (session.deviceState == gs::DeviceState::Lost) {
      wanted = true;
      replug = true;
    } else if (!pendingFor(session)) {
      wanted = true;
    }
    waiting.push_back(*session.device);
  }
  if (!wanted || pending_.contains(key)) {
    return;
  }
  if (open_.contains(key)) {
    sessions_.deviceReappeared(identity, now);
    out.log.push_back(std::format("{}/{} reconnected", driver, device.id));
    return;
  }
  if (device.unavailable != bitoscope::ipc::UnavailableReason::none && !heldHere(key) && watching(driver)) {
    const std::string why = unavailableWords(device);
    for (const gs::DeviceIdentity& own : waiting) {
      sessions_.deviceUnavailable(own.driver, own.id, unavailableKind(device.unavailable), why);
    }
    out.log.push_back(std::format("{}/{} unavailable: {}", driver, device.id, why));
    return;
  }
  for (const gs::DeviceIdentity& own : waiting) {
    sessions_.deviceConnecting(own.driver, own.id);
  }
  requestOpen(key, replug, out);
}

void DeviceRouter::settleDriver(std::string_view driver, RouterOutput& out) {
  const DriverEntry* entry = catalog_.driver(driver);
  if (entry == nullptr || entry->state == ListingState::Listing) {
    return;
  }
  std::set<gs::SessionId> matched;
  if (entry->state == ListingState::Listed) {
    const std::vector<bitoscope::ipc::DeviceInfo> devices = entry->devices;
    for (const auto& device : devices) {
      offerDevice(driver, device, out, &matched);
    }
  }
  const std::string why = entry->state == ListingState::Failed
                              ? std::format("could not list devices: {}", entry->problem)
                              : std::string("not connected");
  for (const gs::Session& session : std::vector<gs::Session>(sessions_.sessions())) {
    if (session.device.has_value() && session.device->driver == driver &&
        session.deviceState == gs::DeviceState::Connecting && !matched.contains(session.id) &&
        !pendingFor(session)) {
      sessions_.deviceLost(session.device->driver, session.device->id, why);
    }
  }
}

RouterOutput DeviceRouter::deviceAppeared(std::string_view driver,
                                          const bitoscope::ipc::DeviceInfo& device) {
  RouterOutput out;
  catalog_.deviceAppeared(driver, device);
  offerDevice(driver, device, out, nullptr);
  return out;
}

void DeviceRouter::markLost(std::string_view driver, std::string_view deviceId,
                            std::string_view reason, RouterOutput& out) {
  open_.erase(Key{std::string(driver), std::string(deviceId)});
  sessions_.deviceLost(driver, deviceId, reason);
  out.log.push_back(std::format("{}/{} lost: {}", driver, deviceId, reason));
}

RouterOutput DeviceRouter::deviceLost(std::string_view driver, std::string_view deviceId,
                                      std::string_view reason) {
  RouterOutput out;
  catalog_.deviceLost(driver, deviceId);
  markLost(driver, deviceId, reason, out);
  return out;
}

RouterOutput DeviceRouter::hostDeviceLost(std::string_view pluginId, std::string_view reason) {
  RouterOutput out;
  const auto split = splitPluginId(pluginId);
  if (!split) {
    out.log.push_back(std::format("Driver that is not \"<driver>/<device>\" ({}) reported its "
                                  "device lost: {}",
                                  pluginId, reason));
    return out;
  }
  markLost(split->first, split->second, reason, out);
  return out;
}

RouterOutput DeviceRouter::watchEnded(std::string_view driver, std::string_view reason) {
  RouterOutput out;
  catalog_.watchEnded(driver, reason);
  out.log.push_back(std::format("{}: no longer watched for devices: {}", driver, reason));
  const std::vector<gs::DeviceIdentity> now = present();
  for (const gs::Session& session : std::vector<gs::Session>(sessions_.sessions())) {
    if (!session.waitsForDevice() || session.device->driver != driver) {
      continue;
    }
    const auto found = gs::findReplugged(*session.device, now);
    if (!found) {
      continue;
    }
    sessions_.deviceConnecting(session.device->driver, session.device->id);
    requestOpen(Key{std::string(driver), now[*found].id}, true, out);
  }
  return out;
}

RouterOutput DeviceRouter::deviceOpened(
    std::string_view driver, std::string_view deviceId,
    const std::expected<bitoscope::host::OpenedDevice, bitoscope::host::OpenRefusal>& opened) {
  RouterOutput out;
  const Key key{std::string(driver), std::string(deviceId)};
  pending_.erase(key);
  progress_.erase(key);
  const bool replug = replug_.erase(key) > 0;
  closing_.erase(key);
  if (opened) {
    plugins_.insert(key);
    opened_[key] = *opened;
  } else {
    plugins_.erase(key);
  }
  const bitoscope::ipc::DeviceInfo* listed = catalog_.find(driver, deviceId);
  gs::DeviceIdentity identity =
    listed != nullptr ? identityOf(driver, *listed)
                      : gs::DeviceIdentity{.driver = std::string(driver), .id = std::string(deviceId),
                                           .serialNumber = {}};
  if (opened && !opened->device.serialNumber.empty()) {
    identity.serialNumber = opened->device.serialNumber;
  }
  std::vector<gs::DeviceIdentity> now = present();
  if (const auto it = std::ranges::find_if(now, [&](const gs::DeviceIdentity& d) {
        return d.driver == identity.driver && d.id == identity.id;
      });
      it != now.end()) {
    *it = identity;
  } else {
    now.push_back(identity);
  }
  if (!opened) {
    const std::string why = std::format("could not be opened: {}", opened.error().message);
    gs::DeviceUnavailable kind = unavailableKind(opened.error().unavailable);
    if (waitedFor(kind) && !watching(driver)) {
      kind = gs::DeviceUnavailable::None;
    }
    for (const gs::Session& session : std::vector<gs::Session>(sessions_.sessions())) {
      if (session.deviceState != gs::DeviceState::Connecting) {
        continue;
      }
      if ((session.device.has_value() && session.device->driver == driver &&
           session.device->id == deviceId) ||
          isFor(session, identity, now)) {
        if (kind == gs::DeviceUnavailable::None) {
          sessions_.deviceLost(session.device->driver, session.device->id, why);
        } else {
          sessions_.deviceUnavailable(session.device->driver, session.device->id, kind, why);
        }
      }
    }
    out.log.push_back(std::format("{}/{} {}", driver, deviceId, why));
    return out;
  }
  open_.insert(key);
  sessions_.deviceReappeared(identity, now);
  out.log.push_back(replug ? std::format("{}/{} reconnected: {}", driver, deviceId, opened->device.name)
                           : std::format("{}/{} opened: {}", driver, deviceId, opened->device.name));
  return out;
}

RouterOutput DeviceRouter::pluginClosed(std::string_view pluginId, std::string_view reason) {
  RouterOutput out;
  const auto split = splitPluginId(pluginId);
  if (!split) {
    return out;
  }
  const Key key{split->first, split->second};
  if (!plugins_.contains(key)) {
    return out;
  }
  plugins_.erase(key);
  if (closing_.erase(key) > 0) {
    out.log.push_back(std::format("{}/{}: its driver was closed to open device afresh", key.first, key.second));
    return out;
  }
  markLost(key.first, key.second, std::format("driver ended: {}", reason), out);
  return out;
}

RouterOutput DeviceRouter::retryWaiting(std::string_view driver) {
  RouterOutput out;
  const std::vector<gs::DeviceIdentity> now = present();
  for (const gs::Session& session : std::vector<gs::Session>(sessions_.sessions())) {
    if (!session.waitsForDevice() || session.device->driver != driver) {
      continue;
    }
    const auto found = gs::findReplugged(*session.device, now);
    if (!found) {
      continue;
    }
    const bitoscope::ipc::DeviceInfo* listed = catalog_.find(driver, now[*found].id);
    if (listed == nullptr || listed->unavailable != bitoscope::ipc::UnavailableReason::none) {
      continue;
    }
    sessions_.deviceConnecting(session.device->driver, session.device->id);
    requestOpen(Key{std::string(driver), listed->id}, true, out);
  }
  return out;
}

RouterOutput DeviceRouter::progressReported(std::string_view pluginId, const bitoscope::ipc::OpenProgress& progress) {
  RouterOutput out;
  const auto split = splitPluginId(pluginId);
  if (!split) {
    return out;
  }
  const Key key{split->first, split->second};
  if (!pending_.contains(key)) {
    return out;
  }
  progress_[key] = progress;
  out.log.push_back(std::format("{}/{} opening: {}", key.first, key.second, progressWords(progress)));
  return out;
}

const bitoscope::ipc::OpenProgress* DeviceRouter::openingProgress(std::string_view driver,
                                                                  std::string_view deviceId) const {
  const auto it = progress_.find(Key{std::string(driver), std::string(deviceId)});
  return it == progress_.end() ? nullptr : &it->second;
}

const bitoscope::ipc::OpenProgress* DeviceRouter::openingProgressFor(const gs::DeviceIdentity& identity) const {
  if (const bitoscope::ipc::OpenProgress* exact = openingProgress(identity.driver, identity.id)) {
    return exact;
  }
  const std::vector<gs::DeviceIdentity> now = present();
  const auto found = gs::findReplugged(identity, now);
  return found ? openingProgress(now[*found].driver, now[*found].id) : nullptr;
}

bool DeviceRouter::pickable(std::string_view driver, const bitoscope::ipc::DeviceInfo& listed) const {
  return listed.unavailable == bitoscope::ipc::UnavailableReason::none ||
         heldHere(Key{std::string(driver), listed.id});
}

std::vector<bitoscope::gui_wizard::WizardDriver> DeviceRouter::wizardDrivers() const {
  std::vector<bitoscope::gui_wizard::WizardDriver> out = catalog_.wizardDrivers();
  const std::vector<DriverEntry>& drivers = catalog_.drivers();
  for (std::size_t i = 0; i < out.size() && i < drivers.size(); ++i) {
    std::erase_if(out[i].unavailable,
                  [&](const std::string& id) { return heldHere(Key{drivers[i].name, id}); });
  }
  return out;
}

const bitoscope::host::OpenedDevice* DeviceRouter::opened(std::string_view driver, std::string_view deviceId) const {
  const auto it = opened_.find(Key{std::string(driver), std::string(deviceId)});
  return it == opened_.end() ? nullptr : &it->second;
}

const bitoscope::host::OpenedDevice* DeviceRouter::openedFor(const gs::DeviceIdentity& identity) const {
  if (!identity.serialNumber.empty()) {
    const bitoscope::host::OpenedDevice* lost = nullptr;
    for (const auto& [key, info] : opened_) {
      if (key.first != identity.driver || info.device.serialNumber != identity.serialNumber) {
        continue;
      }
      if (open_.contains(key)) {
        return &info;
      }
      if (lost == nullptr || key.second == identity.id) {
        lost = &info;
      }
    }
    if (lost != nullptr) {
      return lost;
    }
  }
  return opened(identity.driver, identity.id);
}

const bitoscope::ipc::DeviceInfo* DeviceRouter::openedDevice(std::string_view driver, std::string_view deviceId) const {
  const bitoscope::host::OpenedDevice* found = opened(driver, deviceId);
  return found == nullptr ? nullptr : &found->device;
}

const bitoscope::ipc::DeviceInfo* DeviceRouter::openedDeviceFor(const gs::DeviceIdentity& identity) const {
  const bitoscope::host::OpenedDevice* found = openedFor(identity);
  return found == nullptr ? nullptr : &found->device;
}

bool DeviceRouter::isOpen(std::string_view driver, std::string_view deviceId) const {
  return open_.contains(Key{std::string(driver), std::string(deviceId)});
}

}
