/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <string>
#include <string_view>

#include "device_router.h"
#include "ipc_messages.h"
#include "session_list.h"

namespace bitoscope::gui_devices {

enum class ReasonWords {
  None,
  Busy,
  NoPermission,
  FirmwareLoading,
  Other,
  WaitingForDevice,
};

ReasonWords reasonWords(bitoscope::ipc::UnavailableReason reason);
ReasonWords reasonWords(bitoscope::gui_session::DeviceUnavailable reason);
ReasonWords reasonWords(bitoscope::ipc::OpenProgressCode code);

struct PickerRow {
  std::string label;
  bool selectable = true;
  bool genericName = false;
  ReasonWords reason = ReasonWords::None;
  std::string detail;
  bool operator==(const PickerRow&) const = default;
};

PickerRow pickerRow(const DeviceRouter& router, std::string_view driver, const bitoscope::ipc::DeviceInfo& listed);

enum class HeaderKind { Connected, Connecting, Waiting, Lost };

struct HeaderStatus {
  HeaderKind kind = HeaderKind::Connected;
  ReasonWords reason = ReasonWords::None;
  std::string detail;
  bool operator==(const HeaderStatus&) const = default;
};

HeaderStatus sessionHeaderStatus(const DeviceRouter& router, const bitoscope::gui_session::Session& session);

}
