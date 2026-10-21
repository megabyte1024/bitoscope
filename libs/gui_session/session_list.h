/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "device_identity.h"

namespace bitoscope::gui_session {

using SessionId = std::uint64_t;

enum class DeviceState {
  Connected,
  Connecting,
  Lost,
};

enum class DeviceUnavailable {
  None,
  Busy,
  NoPermission,
  FirmwareLoading,
  Other,
};

struct Session {
  SessionId id = 0;
  std::string name;
  bool running = false;
  std::optional<DeviceIdentity> device{};
  DeviceState deviceState = DeviceState::Connected;
  std::string lostReason{};
  DeviceUnavailable unavailable = DeviceUnavailable::None;
  std::string openedFrom{};

  bool waitsForDevice() const;
};

enum class DeviceReasonKind {
  Busy,
  Lost,
  Waiting,
  NoDevice,
  Connecting,
  OpenedFromFile,
};

struct DeviceDisabledReason {
  DeviceReasonKind kind = DeviceReasonKind::Busy;
  std::string detail;
  DeviceUnavailable unavailable = DeviceUnavailable::None;
  bool operator==(const DeviceDisabledReason&) const = default;
};

std::string reasonText(const DeviceDisabledReason& reason);

enum class StartStop { Start, Stop };

class SessionList {
 public:
  using NameMaker = std::function<std::string(std::uint64_t n)>;

  SessionList() = default;

  static SessionList fromNames(std::span<const std::string> names);

  std::string nextFreeName(const NameMaker& makeName) const;

  SessionId add(const NameMaker& makeName);

  bool canClose(SessionId id) const;
  bool close(SessionId id);

  const std::vector<Session>& sessions() const { return sessions_; }
  const Session* find(SessionId id) const;
  std::vector<std::string> names() const;
  std::vector<std::string> persistentNames() const;

  void setOpenedFrom(SessionId id, std::string path);

  std::optional<SessionId> active() const { return active_; }
  void setActive(SessionId id);

  StartStop startStopAction(SessionId id) const;
  StartStop toggleRunning(SessionId id);

  bool canSave(SessionId id) const;

  bool canStartStopActive() const;
  bool canSaveActive() const;

  void setDevice(SessionId id, DeviceIdentity device);
  const DeviceIdentity* device(SessionId id) const;

  bool startCapture(SessionId id);

  void endCapture(SessionId id);

  void deviceLost(std::string_view driver, std::string_view deviceId, std::string_view reason);

  void deviceUnavailable(std::string_view driver, std::string_view deviceId, DeviceUnavailable why,
                         std::string_view reason);

  void deviceConnecting(std::string_view driver, std::string_view deviceId);

  void deviceReappeared(const DeviceIdentity& identity);

  void deviceReappeared(const DeviceIdentity& identity, std::span<const DeviceIdentity> present);

  std::optional<DeviceDisabledReason> startDisabledReason(SessionId id) const;
  bool canStart(SessionId id) const;

  std::optional<DeviceDisabledReason> stopDisabledReason(SessionId id) const;
  bool canStop(SessionId id) const;

 private:
  Session* findMutable(SessionId id);

  std::vector<Session> sessions_;
  std::optional<SessionId> active_;
  SessionId nextId_ = 1;
};

}
