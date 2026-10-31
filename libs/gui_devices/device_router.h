/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "device_catalog.h"
#include "device_identity.h"
#include "ipc_messages.h"
#include "opened_device.h"
#include "session_list.h"

namespace bitoscope::gui_devices {

struct DeviceRequest {
  std::string driver;
  std::string deviceId;
  bool operator==(const DeviceRequest&) const = default;
};

struct RouterOutput {
  std::vector<DeviceRequest> open;
  std::vector<std::string> log;
};

bitoscope::gui_session::DeviceIdentity identityOf(std::string_view driver,
                                                  const bitoscope::ipc::DeviceInfo& device);

std::optional<std::pair<std::string, std::string>> splitDriverDeviceId(std::string_view driverDeviceId);

class DeviceRouter {
 public:
  explicit DeviceRouter(bitoscope::gui_session::SessionList& sessions);

  const DeviceCatalog& catalog() const { return catalog_; }
  void relisting(std::string_view driver) { catalog_.listing(driver); }

  RouterOutput bindSession(bitoscope::gui_session::SessionId session, std::string_view driver,
                           const bitoscope::ipc::DeviceInfo& device);
  RouterOutput restoreSession(bitoscope::gui_session::SessionId session,
                              const bitoscope::gui_session::DeviceIdentity& identity);

  RouterOutput driversFound(const std::filesystem::path& directory,
                            const std::vector<std::string>& drivers);
  RouterOutput devicesListed(std::string_view driver,
                             const std::expected<bitoscope::ipc::DeviceList, std::string>& list);
  RouterOutput deviceAppeared(std::string_view driver, const bitoscope::ipc::DeviceInfo& device);
  RouterOutput deviceLost(std::string_view driver, std::string_view deviceId,
                          std::string_view reason);
  RouterOutput watchEnded(std::string_view driver, std::string_view reason);
  RouterOutput deviceOpened(std::string_view driver, std::string_view deviceId,
                            const std::expected<bitoscope::host::OpenedDevice, bitoscope::host::OpenRefusal>& opened);
  RouterOutput hostDeviceLost(std::string_view driverDeviceId, std::string_view reason);
  RouterOutput driverClosed(std::string_view driverDeviceId, std::string_view reason);
  RouterOutput retryWaiting(std::string_view driver);
  RouterOutput progressReported(std::string_view driverDeviceId, const bitoscope::ipc::OpenProgress& progress);

  const bitoscope::ipc::OpenProgress* openingProgress(std::string_view driver, std::string_view deviceId) const;
  const bitoscope::ipc::OpenProgress* openingProgressFor(const bitoscope::gui_session::DeviceIdentity& identity) const;

  bool pickable(std::string_view driver, const bitoscope::ipc::DeviceInfo& listed) const;
  std::vector<bitoscope::gui_wizard::WizardDriver> wizardDrivers() const;

  const bitoscope::host::OpenedDevice* opened(std::string_view driver, std::string_view deviceId) const;

  const bitoscope::host::OpenedDevice* openedFor(const bitoscope::gui_session::DeviceIdentity& identity) const;

  const bitoscope::ipc::DeviceInfo* openedDevice(std::string_view driver, std::string_view deviceId) const;

  const bitoscope::ipc::DeviceInfo* openedDeviceFor(
      const bitoscope::gui_session::DeviceIdentity& identity) const;

  bool isOpen(std::string_view driver, std::string_view deviceId) const;

 private:
  using Key = std::pair<std::string, std::string>;

  void requestOpen(const Key& key, bool replug, RouterOutput& out);
  bool heldHere(const Key& key) const;
  bool watching(std::string_view driver) const;
  void offerDevice(std::string_view driver, const bitoscope::ipc::DeviceInfo& device,
                   RouterOutput& out, std::set<bitoscope::gui_session::SessionId>* matched);
  void settleDriver(std::string_view driver, RouterOutput& out);
  void markLost(std::string_view driver, std::string_view deviceId, std::string_view reason,
                RouterOutput& out);
  bool pendingFor(const bitoscope::gui_session::Session& session) const;
  std::vector<bitoscope::gui_session::DeviceIdentity> present() const;
  static bool isFor(const bitoscope::gui_session::Session& session,
                    const bitoscope::gui_session::DeviceIdentity& identity,
                    std::span<const bitoscope::gui_session::DeviceIdentity> present);

  bitoscope::gui_session::SessionList& sessions_;
  DeviceCatalog catalog_;
  std::set<Key> open_;
  std::set<Key> pending_;
  std::set<Key> replug_;
  std::set<Key> drivers_;
  std::set<Key> closing_;
  std::map<Key, bitoscope::host::OpenedDevice> opened_;
  std::map<Key, bitoscope::ipc::OpenProgress> progress_;
};

}
