/*
 * SPDX-License-Identifier: MIT
 */

#include "device_status.h"

#include "device_label.h"

namespace bitoscope::gui_devices {

ReasonWords reasonWords(bitoscope::ipc::UnavailableReason reason) {
  switch (reason) {
  case bitoscope::ipc::UnavailableReason::none:
    return ReasonWords::None;
  case bitoscope::ipc::UnavailableReason::busy:
    return ReasonWords::Busy;
  case bitoscope::ipc::UnavailableReason::noPermission:
    return ReasonWords::NoPermission;
  case bitoscope::ipc::UnavailableReason::firmwareLoading:
    return ReasonWords::FirmwareLoading;
  case bitoscope::ipc::UnavailableReason::other:
    break;
  }
  return ReasonWords::Other;
}

ReasonWords reasonWords(bitoscope::gui_session::DeviceUnavailable reason) {
  using bitoscope::gui_session::DeviceUnavailable;
  switch (reason) {
  case DeviceUnavailable::None:
    return ReasonWords::None;
  case DeviceUnavailable::Busy:
    return ReasonWords::Busy;
  case DeviceUnavailable::NoPermission:
    return ReasonWords::NoPermission;
  case DeviceUnavailable::FirmwareLoading:
    return ReasonWords::FirmwareLoading;
  case DeviceUnavailable::Other:
    break;
  }
  return ReasonWords::Other;
}

ReasonWords reasonWords(bitoscope::ipc::OpenProgressCode code) {
  switch (code) {
  case bitoscope::ipc::OpenProgressCode::firmwareLoading:
    return ReasonWords::FirmwareLoading;
  case bitoscope::ipc::OpenProgressCode::waitingForDevice:
    return ReasonWords::WaitingForDevice;
  case bitoscope::ipc::OpenProgressCode::other:
    break;
  }
  return ReasonWords::None;
}

PickerRow pickerRow(const DeviceRouter& router, std::string_view driver, const bitoscope::ipc::DeviceInfo& listed) {
  PickerRow row;
  row.label = pickerDeviceLabel(router, driver, listed);
  row.selectable = router.pickable(driver, listed);
  const bitoscope::ipc::DeviceInfo* opened =
    router.isOpen(driver, listed.id) ? router.openedDevice(driver, listed.id) : nullptr;
  const bool shownFromHandshake = opened != nullptr && !opened->name.empty();
  row.genericName = !shownFromHandshake && !listed.identified && !listed.name.empty();
  if (!row.selectable) {
    row.reason = reasonWords(listed.unavailable);
    row.detail = listed.unavailableDetail;
  }
  return row;
}

HeaderStatus sessionHeaderStatus(const DeviceRouter& router, const bitoscope::gui_session::Session& session) {
  using bitoscope::gui_session::DeviceState;
  switch (session.deviceState) {
  case DeviceState::Connected:
    return {.kind = HeaderKind::Connected, .reason = ReasonWords::None, .detail = {}};
  case DeviceState::Lost:
    if (session.waitsForDevice()) {
      return {.kind = HeaderKind::Waiting, .reason = reasonWords(session.unavailable), .detail = session.lostReason};
    }
    return {.kind = HeaderKind::Lost, .detail = session.lostReason};
  case DeviceState::Connecting:
    break;
  }
  if (session.device.has_value()) {
    if (const bitoscope::ipc::OpenProgress* progress = router.openingProgressFor(*session.device)) {
      return {.kind = HeaderKind::Connecting, .reason = reasonWords(progress->code), .detail = progress->detail};
    }
  }
  return {.kind = HeaderKind::Connecting, .reason = ReasonWords::None, .detail = {}};
}

}
