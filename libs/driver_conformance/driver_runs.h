/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "driver_conformance.h"
#include "ipc_messages.h"

namespace bitoscope::driver_conformance {

struct InterfaceRun {
  std::string output;
  std::optional<int> exitCode;
  std::vector<std::string> problems;
};

struct ListRun {
  std::optional<ipc::DeviceList> list;
  std::vector<std::string> problems;
};

struct DeviceRun {
  std::optional<ipc::HandshakeReply> reply;
  std::vector<ipc::OpenProgress> progressBeforeReply;
  std::vector<ipc::OpenProgress> progressAfterReply;
  std::vector<std::string> problems;
  std::optional<int> exitCode;
  std::optional<std::string> exitProblem;
};

std::vector<std::string> modeArguments(std::string_view mode, const host::DriverConnection& connection);

InterfaceRun runInterface(const ConformanceOptions& options);

ListRun runList(const ConformanceOptions& options, const std::string& label);

DeviceRun runDevice(const ConformanceOptions& options, const std::string& deviceId);

}
