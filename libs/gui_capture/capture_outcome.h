/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "acquisition.h"
#include "capture_session.h"

namespace bitoscope::gui_capture {

enum class OutcomeKind {
  Complete,
  ShortMemory,
  ShortStoppedByUser,
  ShortStoppedWithSession,
  Failed,
};

struct CaptureOutcome {
  OutcomeKind kind = OutcomeKind::Failed;
  uint64_t requestedSamples = 0;
  uint64_t storedSamples = 0;
  std::optional<uint64_t> requestedPostTriggerSamples;
  std::optional<uint64_t> storedPostTriggerSamples;
  std::string message;
  std::shared_ptr<bitoscope::capture::CaptureSession> session;

  std::shared_ptr<const bitoscope::capture::RawCaptureBuffer> buffer;
  bitoscope::capture::CaptureExtent extent;
  bool trimmed = false;
  std::optional<uint64_t> originSampleIndex;
  std::optional<uint64_t> triggerSampleIndex;
  std::optional<uint64_t> requestedPreTriggerSamples;
  std::optional<bitoscope::capture::MemoryShortfall> shortfall;
  std::string device;
  uint64_t captureId = 0;
  std::vector<bitoscope::ipc::props::PropertyValue> sentProperties;

  bool kept() const { return kind != OutcomeKind::Failed; }
  bool isShort() const {
    return kind == OutcomeKind::ShortMemory || kind == OutcomeKind::ShortStoppedByUser ||
           kind == OutcomeKind::ShortStoppedWithSession;
  }
};

CaptureOutcome outcomeOf(const std::expected<bitoscope::acquisition::SessionResult, std::string>& result);

uint64_t timeZeroSample(std::optional<uint64_t> originSampleIndex, std::optional<uint64_t> triggerSampleIndex);
uint64_t timeZeroSample(const CaptureOutcome& outcome);

}
