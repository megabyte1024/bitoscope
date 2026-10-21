/*
 * SPDX-License-Identifier: MIT
 */

#include "capture_status.h"

namespace bitoscope::gui_capture {

namespace {

std::string_view shortReason(OutcomeKind kind) {
  switch (kind) {
  case OutcomeKind::ShortMemory:
    return "memory full";
  case OutcomeKind::ShortStoppedByUser:
    return "stopped by user";
  case OutcomeKind::ShortStoppedWithSession:
    return "stopped with session";
  case OutcomeKind::Complete:
  case OutcomeKind::Failed:
    break;
  }
  return {};
}

}

CaptureStatus captureStatusOf(const PanelCapture& capture) {
  CaptureStatus status;
  if (capture.running()) {
    status.state = CaptureState::Running;
    if (const auto& progress = capture.lastProgress()) {
      status.storedSamples = progress->stored_samples;
      status.requestedSamples = progress->requested_samples;
      if (progress->armed) {
        status.state = CaptureState::WaitingForTrigger;
      }
    }
    return status;
  }
  if (const auto& kept = capture.kept()) {
    status.state = CaptureState::Done;
    status.storedSamples = kept->storedSamples;
    status.requestedSamples = kept->requestedSamples;
    status.reason = std::string(shortReason(kept->kind));
    return status;
  }
  if (const auto& error = capture.lastError()) {
    status.state = CaptureState::Failed;
    status.reason = *error;
  }
  return status;
}

std::string_view captureStateName(CaptureState state) {
  switch (state) {
  case CaptureState::Idle:
    return "idle";
  case CaptureState::WaitingForTrigger:
    return "waiting-for-trigger";
  case CaptureState::Running:
    return "running";
  case CaptureState::Done:
    return "done";
  case CaptureState::Failed:
    return "failed";
  }
  return "idle";
}

}
