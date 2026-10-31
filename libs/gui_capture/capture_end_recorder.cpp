/*
 * SPDX-License-Identifier: MIT
 */

#include "capture_end_recorder.h"

#include <variant>

namespace bitoscope::gui_capture {

void CaptureEndRecorder::onFacetEvent(std::string_view driverDeviceId, const bitoscope::ipc::FacetEvent& event) {
  if (event.facet != bitoscope::ipc::la::kFacetName) return;
  auto decoded = bitoscope::ipc::la::decodeEvent(event.payload);
  if (!decoded) return;
  const auto* ended = std::get_if<bitoscope::ipc::la::CaptureEnded>(&*decoded);
  if (ended == nullptr) return;
  std::lock_guard lock(mutex_);
  ended_[{std::string(driverDeviceId), ended->captureId}] = *ended;
}

std::optional<bitoscope::ipc::la::CaptureEnded> CaptureEndRecorder::ended(std::string_view driverDeviceId,
                                                                          uint64_t captureId) const {
  std::lock_guard lock(mutex_);
  const auto it = ended_.find({std::string(driverDeviceId), captureId});
  if (it == ended_.end()) return std::nullopt;
  return it->second;
}

}
