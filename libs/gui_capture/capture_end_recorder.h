/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "ipc_la_acquisition.h"
#include "ipc_messages.h"

namespace bitoscope::gui_capture {

class CaptureEndRecorder {
 public:
  void onFacetEvent(std::string_view pluginId, const bitoscope::ipc::FacetEvent& event);
  std::optional<bitoscope::ipc::la::CaptureEnded> ended(std::string_view pluginId, uint64_t captureId) const;

 private:
  mutable std::mutex mutex_;
  std::map<std::pair<std::string, uint64_t>, bitoscope::ipc::la::CaptureEnded> ended_;
};

}
