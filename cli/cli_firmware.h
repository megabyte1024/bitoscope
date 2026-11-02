/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <chrono>
#include <expected>
#include <functional>
#include <mutex>
#include <optional>
#include <ostream>
#include <string>

#include "cli_args.h"
#include "driver_device.h"
#include "facet_client.h"
#include "ipc_firmware_update.h"
#include "ipc_messages.h"

namespace bitoscope::cli {

class FirmwareEvents {
 public:
  void onFacetEvent(const ipc::FacetEvent& event);
  std::optional<ipc::firmware::Progress> progress() const;
  std::optional<ipc::firmware::Finished> finished() const;

 private:
  mutable std::mutex mutex_;
  std::optional<ipc::firmware::Progress> progress_;
  std::optional<ipc::firmware::Finished> finished_;
};

struct FirmwareSeams {
  std::function<bool()> interrupted;
  std::function<void()> clear_interrupt;
  std::function<void(std::chrono::milliseconds)> sleep;
  std::chrono::milliseconds poll{50};
  std::chrono::milliseconds confirm_window{5000};
  std::chrono::milliseconds status_every{2000};
  std::chrono::milliseconds report_every{10000};
  std::chrono::milliseconds program_start_wait{5000};
  std::function<std::optional<std::string>()> driver_closed;
  std::function<std::expected<host::pipe::DriverDevice, std::string>()> reopen;
  std::function<std::expected<std::string, std::string>(const host::pipe::DriverDevice&)> capture_check;
};

std::string describeFirmwareProgress(const ipc::firmware::Progress& progress);

int runFlashFirmware(const CliOptions& opts, const host::pipe::DriverDevice& session, host::FacetTransport& transport,
                     FirmwareEvents& events, std::ostream& out, std::ostream& err, const FirmwareSeams& seams);

int runStartProgram(const host::pipe::DriverDevice& session, host::FacetTransport& transport, FirmwareEvents& events,
                    std::ostream& out, std::ostream& err, const FirmwareSeams& seams);

}
