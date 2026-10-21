/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstddef>
#include <expected>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "ipc_messages.h"
#include "new_session_wizard.h"

namespace bitoscope::gui_devices {

enum class ListingState {
  Listing,
  Listed,
  Failed,
};

struct DriverEntry {
  std::string name;
  std::string displayName;
  std::string pluginVersion;
  ListingState state = ListingState::Listing;
  std::string problem;
  bool supportsWatch = false;
  bool watching = false;
  std::string watchProblem;
  bool answered = false;
  std::vector<bitoscope::ipc::DeviceInfo> devices;

  const std::string& shownName() const { return displayName.empty() ? name : displayName; }
};

class DeviceCatalog {
 public:
  void driversFound(std::filesystem::path directory, std::vector<std::string> drivers);
  bool driversKnown() const { return driversKnown_; }
  const std::filesystem::path& directory() const { return directory_; }
  const std::vector<DriverEntry>& drivers() const { return drivers_; }
  const DriverEntry* driver(std::string_view name) const;

  void listing(std::string_view driver);
  void listed(std::string_view driver,
              const std::expected<bitoscope::ipc::DeviceList, std::string>& list);

  void deviceAppeared(std::string_view driver, const bitoscope::ipc::DeviceInfo& device);
  void deviceLost(std::string_view driver, std::string_view deviceId);
  void watchEnded(std::string_view driver, std::string_view reason);

  const bitoscope::ipc::DeviceInfo* find(std::string_view driver, std::string_view deviceId) const;

  std::vector<bitoscope::gui_wizard::WizardDriver> wizardDrivers() const;

  std::vector<std::string> driversToRelist() const;

 private:
  DriverEntry* mutableDriver(std::string_view name);

  bool driversKnown_ = false;
  std::filesystem::path directory_;
  std::vector<DriverEntry> drivers_;
};

}
