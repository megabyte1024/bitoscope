/*
 * SPDX-License-Identifier: MIT
 */

#include "new_session_wizard.h"

#include <display_format.h>

#include <algorithm>
#include <utility>

namespace bitoscope::gui_wizard {

namespace {

bool isDigit(char c) {
  return c >= '0' && c <= '9';
}

// WHY: Numerically equal runs that differ in leading zeros fall back to text, so the order stays strict.
bool naturalLess(std::string_view a, std::string_view b) {
  std::size_t i = 0;
  std::size_t j = 0;
  while (i < a.size() && j < b.size()) {
    if (isDigit(a[i]) && isDigit(b[j])) {
      std::size_t iEnd = i;
      std::size_t jEnd = j;
      while (iEnd < a.size() && isDigit(a[iEnd])) {
        ++iEnd;
      }
      while (jEnd < b.size() && isDigit(b[jEnd])) {
        ++jEnd;
      }
      std::string_view x = a.substr(i, iEnd - i);
      std::string_view y = b.substr(j, jEnd - j);
      const auto trimmed = [](std::string_view digits) {
        const std::size_t first = digits.find_first_not_of('0');
        return first == std::string_view::npos ? std::string_view{} : digits.substr(first);
      };
      const std::string_view tx = trimmed(x);
      const std::string_view ty = trimmed(y);
      if (tx.size() != ty.size()) {
        return tx.size() < ty.size();
      }
      if (tx != ty) {
        return tx < ty;
      }
      if (x != y) {
        return x < y;
      }
      i = iEnd;
      j = jEnd;
      continue;
    }
    if (a[i] != b[j]) {
      return static_cast<unsigned char>(a[i]) < static_cast<unsigned char>(b[j]);
    }
    ++i;
    ++j;
  }
  return a.size() - i < b.size() - j;
}

}

std::expected<std::uint16_t, PortProblem> parsePort(std::string_view text) {
  if (text.empty()) {
    return std::unexpected(PortProblem::Empty);
  }
  if (!std::ranges::all_of(text, isDigit)) {
    return std::unexpected(PortProblem::NotANumber);
  }
  if (text.size() > 1 && text.front() == '0') {
    return std::unexpected(PortProblem::NotANumber);
  }
  if (text.size() > 5) {
    return std::unexpected(PortProblem::OutOfRange);
  }
  std::uint32_t value = 0;
  for (const char c : text) {
    value = value * 10 + static_cast<std::uint32_t>(c - '0');
  }
  if (value < 1 || value > 65'535) {
    return std::unexpected(PortProblem::OutOfRange);
  }
  return static_cast<std::uint16_t>(value);
}

std::vector<std::string> sortSerialPorts(std::vector<std::string> ports) {
  std::ranges::sort(ports, naturalLess);
  const auto repeats = std::ranges::unique(ports);
  ports.erase(repeats.begin(), repeats.end());
  return ports;
}

NewSessionWizard::NewSessionWizard(std::vector<WizardDriver> drivers,
                                   std::vector<std::string> serialPorts)
    : drivers_(std::move(drivers)),
      serialPorts_(sortSerialPorts(std::move(serialPorts))),
      picked_(drivers_.size()) {}

const std::vector<std::string>& NewSessionWizard::devices(std::size_t driver) const {
  static const std::vector<std::string> kNone;
  return driver < drivers_.size() ? drivers_[driver].devices : kNone;
}

const std::vector<std::string>& NewSessionWizard::unavailable(std::size_t driver) const {
  static const std::vector<std::string> kNone;
  return driver < drivers_.size() ? drivers_[driver].unavailable : kNone;
}

void NewSessionWizard::setDevices(std::size_t driver, std::vector<std::string> deviceIds,
                                  std::vector<std::string> unavailableIds) {
  if (driver >= drivers_.size()) {
    return;
  }
  drivers_[driver].devices = std::move(deviceIds);
  drivers_[driver].unavailable = std::move(unavailableIds);
  std::optional<std::string>& picked = picked_[driver];
  if (picked && std::ranges::find(drivers_[driver].devices, *picked) == drivers_[driver].devices.end()) {
    picked.reset();  // WHY: unplugged: not picked again by itself if it comes back
  }
  if (picked && std::ranges::find(drivers_[driver].unavailable, *picked) != drivers_[driver].unavailable.end()) {
    picked.reset();
  }
}

std::optional<std::size_t> NewSessionWizard::listedDevice() const {
  if (!driver_ || !picked_[*driver_]) {
    return std::nullopt;
  }
  const std::vector<std::string>& list = drivers_[*driver_].devices;
  const auto it = std::ranges::find(list, *picked_[*driver_]);
  if (it == list.end()) {
    return std::nullopt;
  }
  return static_cast<std::size_t>(it - list.begin());
}

std::optional<DeviceSearch> NewSessionWizard::search() const {
  if (!driver_) {
    return std::nullopt;
  }
  return drivers_[*driver_].search;
}

void NewSessionWizard::selectDriver(std::size_t index) {
  if (page_ == WizardPage::Driver && index < drivers_.size()) {
    driver_ = index;
  }
}

bool NewSessionWizard::canGoNext() const {
  return page_ == WizardPage::Driver && driver_.has_value();
}

bool NewSessionWizard::next() {
  if (!canGoNext()) {
    return false;
  }
  page_ = WizardPage::Device;
  return true;
}

bool NewSessionWizard::canGoBack() const {
  return page_ == WizardPage::Device;
}

bool NewSessionWizard::back() {
  if (!canGoBack()) {
    return false;
  }
  page_ = WizardPage::Driver;
  return true;
}

bool NewSessionWizard::canSelectListedDevice(std::size_t index) const {
  if (!driver_ || index >= drivers_[*driver_].devices.size()) {
    return false;
  }
  const WizardDriver& driver = drivers_[*driver_];
  return std::ranges::find(driver.unavailable, driver.devices[index]) == driver.unavailable.end();
}

void NewSessionWizard::selectListedDevice(std::size_t index) {
  if (canSelectListedDevice(index)) {
    picked_[*driver_] = drivers_[*driver_].devices[index];
  }
}

void NewSessionWizard::selectSerialPort(std::size_t index) {
  if (index < serialPorts_.size()) {
    serialPort_ = index;
  }
}

bool NewSessionWizard::selectBaudRate(std::uint32_t baudRate) {
  if (std::ranges::find(kBaudRates, baudRate) == kBaudRates.end()) {
    return false;
  }
  baudRate_ = baudRate;
  return true;
}

void NewSessionWizard::setAddressText(std::string text) {
  addressText_ = std::move(text);
}

std::optional<AddressProblem> NewSessionWizard::addressProblem() const {
  if (addressText_.empty()) {
    return AddressProblem::Empty;
  }
  if (!gui_format::parseIpv4(addressText_)) {
    return AddressProblem::Invalid;
  }
  return std::nullopt;
}

void NewSessionWizard::setPortText(std::string text) {
  portText_ = std::move(text);
}

std::optional<PortProblem> NewSessionWizard::portProblem() const {
  const auto port = parsePort(portText_);
  if (!port) {
    return port.error();
  }
  return std::nullopt;
}

bool NewSessionWizard::canCreate() const {
  return create().has_value();
}

std::optional<NewDevice> NewSessionWizard::create() const {
  if (page_ != WizardPage::Device || !driver_) {
    return std::nullopt;
  }
  NewDevice device;
  device.driver = *driver_;
  device.search = drivers_[*driver_].search;
  switch (device.search) {
  case DeviceSearch::DeviceList: {
    const auto index = listedDevice();
    if (!index || *index >= drivers_[*driver_].devices.size()) {
      return std::nullopt;
    }
    device.deviceId = drivers_[*driver_].devices[*index];
    break;
  }
  case DeviceSearch::SerialPort:
    if (!serialPort_) {
      return std::nullopt;
    }
    device.serialPort = serialPorts_[*serialPort_];
    device.baudRate = baudRate_;
    break;
  case DeviceSearch::Network: {
    const auto address = gui_format::parseIpv4(addressText_);
    const auto port = parsePort(portText_);
    if (!address || !port) {
      return std::nullopt;
    }
    device.address = *address;
    device.port = *port;
    break;
  }
  case DeviceSearch::SingleDevice:
    break;
  }
  return device;
}

bool NewSessionWizard::isListOnlyPage() const {
  if (page_ == WizardPage::Driver) {
    return true;
  }
  return search() == DeviceSearch::DeviceList;
}

std::optional<NewDevice> NewSessionWizard::activateListItem(std::size_t index) {
  if (!isListOnlyPage()) {
    return std::nullopt;
  }
  if (page_ == WizardPage::Driver) {
    selectDriver(index);
    if (canGoNext()) {
      next();
    }
    return std::nullopt;
  }
  if (!canSelectListedDevice(index)) {
    return std::nullopt;
  }
  selectListedDevice(index);
  if (canCreate()) {
    return create();
  }
  return std::nullopt;
}

}
