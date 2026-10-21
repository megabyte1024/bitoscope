/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "panel_capture.h"

namespace bitoscope::gui_capture {

enum class CaptureState {
  Idle,
  WaitingForTrigger,
  Running,
  Done,
  Failed,
};

struct CaptureStatus {
  CaptureState state = CaptureState::Idle;
  std::uint64_t storedSamples = 0;
  std::uint64_t requestedSamples = 0;
  std::string reason;
};

CaptureStatus captureStatusOf(const PanelCapture& capture);

std::string_view captureStateName(CaptureState state);

}
