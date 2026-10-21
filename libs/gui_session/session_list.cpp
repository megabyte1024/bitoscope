/*
 * SPDX-License-Identifier: MIT
 */

#include "session_list.h"

#include <algorithm>
#include <format>
#include <utility>

namespace bitoscope::gui_session {

bool Session::waitsForDevice() const {
  return deviceState == DeviceState::Lost &&
         (unavailable == DeviceUnavailable::Busy || unavailable == DeviceUnavailable::FirmwareLoading);
}

SessionList SessionList::fromNames(std::span<const std::string> names) {
  SessionList list;
  for (const std::string& name : names) {
    if (name.empty()) {
      continue;
    }
    const bool repeated = std::ranges::any_of(
      list.sessions_, [&name](const Session& session) { return session.name == name; });
    if (!repeated) {
      list.sessions_.push_back({list.nextId_++, name, false});
    }
  }
  if (!list.sessions_.empty()) {
    list.active_ = list.sessions_.front().id;
  }
  return list;
}

std::string SessionList::nextFreeName(const NameMaker& makeName) const {
  const auto inUse = [this](const std::string& name) {
    return std::ranges::any_of(sessions_,
                               [&name](const Session& session) { return session.name == name; });
  };
  // WHY: With n sessions at most n of the first n + 1 candidate names are taken, so n + 1 tries always find a
  // free one.
  const std::uint64_t tries = sessions_.size() + 1;
  for (std::uint64_t n = 1; n <= tries; ++n) {
    std::string name = makeName(n);
    if (!name.empty() && !inUse(name)) {
      return name;
    }
  }
  const std::string base = makeName(1).empty() ? std::string("?") : makeName(1);
  for (std::uint64_t k = 2;; ++k) {
    std::string name = std::format("{} ({})", base, k);
    if (!inUse(name)) {
      return name;
    }
  }
}

SessionId SessionList::add(const NameMaker& makeName) {
  const SessionId id = nextId_++;
  sessions_.push_back({id, nextFreeName(makeName), false});
  active_ = id;
  return id;
}

bool SessionList::canClose(SessionId id) const {
  const Session* session = find(id);
  return session != nullptr && !session->running;
}

bool SessionList::close(SessionId id) {
  if (!canClose(id)) {
    return false;
  }
  const auto it = std::ranges::find(sessions_, id, &Session::id);
  const auto index = static_cast<std::size_t>(it - sessions_.begin());
  sessions_.erase(it);
  if (active_ == id) {
    if (sessions_.empty()) {
      active_.reset();
    } else {
      active_ = sessions_[(std::min)(index, sessions_.size() - 1)].id;
    }
  }
  return true;
}

const Session* SessionList::find(SessionId id) const {
  const auto it = std::ranges::find(sessions_, id, &Session::id);
  return it == sessions_.end() ? nullptr : &*it;
}

Session* SessionList::findMutable(SessionId id) {
  const auto it = std::ranges::find(sessions_, id, &Session::id);
  return it == sessions_.end() ? nullptr : &*it;
}

std::vector<std::string> SessionList::names() const {
  std::vector<std::string> out;
  out.reserve(sessions_.size());
  for (const Session& session : sessions_) {
    out.push_back(session.name);
  }
  return out;
}

std::vector<std::string> SessionList::persistentNames() const {
  std::vector<std::string> out;
  for (const Session& session : sessions_) {
    if (session.openedFrom.empty()) {
      out.push_back(session.name);
    }
  }
  return out;
}

void SessionList::setOpenedFrom(SessionId id, std::string path) {
  if (Session* s = findMutable(id)) {
    s->openedFrom = std::move(path);
  }
}

void SessionList::setActive(SessionId id) {
  if (find(id) != nullptr) {
    active_ = id;
  }
}

StartStop SessionList::startStopAction(SessionId id) const {
  const Session* session = find(id);
  return session != nullptr && session->running ? StartStop::Stop : StartStop::Start;
}

StartStop SessionList::toggleRunning(SessionId id) {
  if (Session* session = findMutable(id)) {
    session->running = !session->running;
  }
  return startStopAction(id);
}

bool SessionList::canSave(SessionId id) const {
  const Session* session = find(id);
  return session != nullptr && !session->running;
}

bool SessionList::canStartStopActive() const {
  return active_.has_value() && find(*active_) != nullptr;
}

bool SessionList::canSaveActive() const {
  return active_.has_value() && canSave(*active_);
}

std::string reasonText(const DeviceDisabledReason& reason) {
  switch (reason.kind) {
    case DeviceReasonKind::Busy:
      return std::format("In use by {}", reason.detail);
    case DeviceReasonKind::Lost:
      return std::format("Device lost: {}", reason.detail);
    case DeviceReasonKind::Waiting:
      return std::format("Waiting for device: {}", reason.detail);
    case DeviceReasonKind::NoDevice:
      return "No device";
    case DeviceReasonKind::Connecting:
      return "Connecting to device";
    case DeviceReasonKind::OpenedFromFile:
      return std::format("Opened from file: {}", reason.detail);
  }
  return {};  // WHY: A switch over an enum class with no default keeps MSVC /W4 quiet and flags a future unhandled case.
}

void SessionList::setDevice(SessionId id, DeviceIdentity device) {
  if (Session* s = findMutable(id)) {
    s->device = std::move(device);
    s->deviceState = DeviceState::Connected;
    s->lostReason.clear();
    s->unavailable = DeviceUnavailable::None;
  }
}

const DeviceIdentity* SessionList::device(SessionId id) const {
  const Session* s = find(id);
  return (s != nullptr && s->device.has_value()) ? &*s->device : nullptr;
}

bool SessionList::startCapture(SessionId id) {
  if (!canStart(id)) {
    return false;
  }
  Session* s = findMutable(id);
  s->running = true;
  return true;
}

void SessionList::endCapture(SessionId id) {
  if (Session* s = findMutable(id)) {
    s->running = false;
  }
}

void SessionList::deviceLost(std::string_view driver, std::string_view deviceId,
                             std::string_view reason) {
  for (Session& s : sessions_) {
    if (s.device.has_value() && s.device->driver == driver && s.device->id == deviceId) {
      s.deviceState = DeviceState::Lost;
      s.lostReason = std::string(reason);
      s.unavailable = DeviceUnavailable::None;
    }
  }
}

void SessionList::deviceUnavailable(std::string_view driver, std::string_view deviceId, DeviceUnavailable why,
                                    std::string_view reason) {
  for (Session& s : sessions_) {
    if (s.device.has_value() && s.device->driver == driver && s.device->id == deviceId) {
      s.deviceState = DeviceState::Lost;
      s.lostReason = std::string(reason);
      s.unavailable = why;
    }
  }
}

void SessionList::deviceConnecting(std::string_view driver, std::string_view deviceId) {
  for (Session& s : sessions_) {
    if (s.device.has_value() && s.device->driver == driver && s.device->id == deviceId) {
      s.deviceState = DeviceState::Connecting;
      s.lostReason.clear();
      s.unavailable = DeviceUnavailable::None;
    }
  }
}

void SessionList::deviceReappeared(const DeviceIdentity& identity,
                                   std::span<const DeviceIdentity> present) {
  for (Session& s : sessions_) {
    if (s.deviceState == DeviceState::Connected || !s.device.has_value()) {
      continue;
    }
    const auto found = findReplugged(*s.device, present);
    if (found.has_value() && present[*found] == identity) {
      s.device = identity;
      s.deviceState = DeviceState::Connected;
      s.lostReason.clear();
      s.unavailable = DeviceUnavailable::None;
    }
  }
}

void SessionList::deviceReappeared(const DeviceIdentity& identity) {
  deviceReappeared(identity, std::span<const DeviceIdentity>(&identity, 1));
}

std::optional<DeviceDisabledReason> SessionList::startDisabledReason(SessionId id) const {
  const Session* s = find(id);
  if (s == nullptr || s->running) {
    return std::nullopt;
  }
  if (!s->device.has_value()) {
    if (!s->openedFrom.empty()) {
      return DeviceDisabledReason{.kind = DeviceReasonKind::OpenedFromFile, .detail = s->openedFrom};
    }
    return DeviceDisabledReason{.kind = DeviceReasonKind::NoDevice, .detail = {}};
  }
  if (s->deviceState == DeviceState::Lost) {
    return DeviceDisabledReason{.kind = s->waitsForDevice() ? DeviceReasonKind::Waiting : DeviceReasonKind::Lost,
                                .detail = s->lostReason,
                                .unavailable = s->unavailable};
  }
  if (s->deviceState == DeviceState::Connecting) {
    return DeviceDisabledReason{.kind = DeviceReasonKind::Connecting, .detail = {}};
  }
  for (const Session& other : sessions_) {
    if (other.id == id) {
      continue;
    }
    if (other.running && other.device.has_value() && *other.device == *s->device) {
      return DeviceDisabledReason{.kind = DeviceReasonKind::Busy, .detail = other.name};
    }
  }
  return std::nullopt;
}

bool SessionList::canStart(SessionId id) const {
  const Session* s = find(id);
  if (s == nullptr || s->running) {
    return false;
  }
  return !startDisabledReason(id).has_value();
}

std::optional<DeviceDisabledReason> SessionList::stopDisabledReason(SessionId id) const {
  const Session* s = find(id);
  if (s == nullptr || !s->running) {
    return std::nullopt;
  }
  if (s->deviceState == DeviceState::Lost) {
    return DeviceDisabledReason{.kind = DeviceReasonKind::Lost, .detail = s->lostReason, .unavailable = s->unavailable};
  }
  return std::nullopt;
}

bool SessionList::canStop(SessionId id) const {
  const Session* s = find(id);
  return s != nullptr && s->running && !stopDisabledReason(id).has_value();
}

}
