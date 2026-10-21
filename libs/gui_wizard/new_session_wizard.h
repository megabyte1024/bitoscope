/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace bitoscope::gui_wizard {

enum class DeviceSearch {
  DeviceList,
  SerialPort,
  Network,
  SingleDevice,
};

struct WizardDriver {
  DeviceSearch search = DeviceSearch::DeviceList;
  std::vector<std::string> devices;
  std::vector<std::string> unavailable{};
};

enum class WizardPage { Driver, Device };

inline constexpr std::array<std::uint32_t, 8> kBaudRates{9'600,   19'200,  38'400,  57'600,
                                                        115'200, 230'400, 460'800, 921'600};
inline constexpr std::uint32_t kDefaultBaudRate = 115'200;

enum class AddressProblem {
  Empty,
  Invalid,
};
enum class PortProblem {
  Empty,
  NotANumber,
  OutOfRange,
};

std::expected<std::uint16_t, PortProblem> parsePort(std::string_view text);

std::vector<std::string> sortSerialPorts(std::vector<std::string> ports);

struct NewDevice {
  std::size_t driver = 0;
  DeviceSearch search = DeviceSearch::SingleDevice;
  std::string deviceId;
  std::string serialPort;
  std::uint32_t baudRate = 0;
  std::uint32_t address = 0;
  std::uint16_t port = 0;

  bool operator==(const NewDevice&) const = default;
};

class NewSessionWizard {
 public:
  NewSessionWizard(std::vector<WizardDriver> drivers, std::vector<std::string> serialPorts);

  const std::vector<std::string>& devices(std::size_t driver) const;
  const std::vector<std::string>& unavailable(std::size_t driver) const;
  void setDevices(std::size_t driver, std::vector<std::string> deviceIds,
                  std::vector<std::string> unavailableIds = {});

  WizardPage page() const { return page_; }

  std::optional<std::size_t> driver() const { return driver_; }
  std::optional<DeviceSearch> search() const;
  void selectDriver(std::size_t index);

  bool canGoNext() const;
  bool next();
  bool canGoBack() const;
  bool back();

  std::optional<std::size_t> listedDevice() const;
  bool canSelectListedDevice(std::size_t index) const;
  void selectListedDevice(std::size_t index);

  const std::vector<std::string>& serialPorts() const { return serialPorts_; }
  std::optional<std::size_t> serialPort() const { return serialPort_; }
  void selectSerialPort(std::size_t index);
  std::uint32_t baudRate() const { return baudRate_; }
  bool selectBaudRate(std::uint32_t baudRate);

  const std::string& addressText() const { return addressText_; }
  void setAddressText(std::string text);
  std::optional<AddressProblem> addressProblem() const;
  const std::string& portText() const { return portText_; }
  void setPortText(std::string text);
  std::optional<PortProblem> portProblem() const;

  bool canCreate() const;
  std::optional<NewDevice> create() const;

  // WHY: Double click activates only list-only pages: other device pages have fields beside their list (owner).
  bool isListOnlyPage() const;

  std::optional<NewDevice> activateListItem(std::size_t index);

 private:
  std::vector<WizardDriver> drivers_;
  std::vector<std::string> serialPorts_;

  WizardPage page_ = WizardPage::Driver;
  std::optional<std::size_t> driver_;
  std::vector<std::optional<std::string>> picked_;
  std::optional<std::size_t> serialPort_;
  std::uint32_t baudRate_ = kDefaultBaudRate;
  std::string addressText_;
  std::string portText_;
};

}
