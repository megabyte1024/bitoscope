/*
 * SPDX-License-Identifier: MIT
 */

#include "capture_outcome.h"

namespace bitoscope::gui_capture {

CaptureOutcome outcomeOf(const std::expected<bitoscope::acquisition::SessionResult, std::string>& result) {
  CaptureOutcome outcome;
  if (!result.has_value()) {
    outcome.kind = OutcomeKind::Failed;
    outcome.message = result.error();
    return outcome;
  }
  if (result->devices.empty()) {
    outcome.kind = OutcomeKind::Failed;
    outcome.message = "capture ended with no device's data";
    return outcome;
  }
  const bitoscope::acquisition::AcquireResult& device = result->devices.front();
  outcome.session = device.session;
  outcome.requestedSamples = device.requested_samples;
  if (device.session) {
    outcome.buffer = std::shared_ptr<const bitoscope::capture::RawCaptureBuffer>(device.session,
                                                                                  &device.session->buffer());
    outcome.extent = device.session->captureExtent();
    outcome.originSampleIndex = device.session->triggerOrigin();
    outcome.triggerSampleIndex = device.session->triggerSampleIndex();
  }
  outcome.requestedPreTriggerSamples = device.requested_pre_trigger_samples;
  outcome.shortfall = device.memory_shortfall;
  outcome.device = device.device;
  outcome.captureId = device.summary.capture_id;
  outcome.sentProperties = device.properties;
  if (!device.memory_shortfall.has_value()) {
    outcome.kind = OutcomeKind::Complete;
    outcome.storedSamples = device.requested_samples;
    outcome.requestedPostTriggerSamples = device.requested_post_trigger_samples;
    outcome.storedPostTriggerSamples = device.requested_post_trigger_samples;
    return outcome;
  }
  const bitoscope::capture::MemoryShortfall& shortfall = *device.memory_shortfall;
  switch (shortfall.reason) {
    case bitoscope::capture::ShortReason::Memory:
      outcome.kind = OutcomeKind::ShortMemory;
      break;
    case bitoscope::capture::ShortReason::StoppedByUser:
      outcome.kind = OutcomeKind::ShortStoppedByUser;
      break;
    case bitoscope::capture::ShortReason::StoppedWithSession:
      outcome.kind = OutcomeKind::ShortStoppedWithSession;
      break;
  }
  outcome.requestedSamples = shortfall.requested_samples;
  outcome.storedSamples = shortfall.stored_samples;
  outcome.requestedPostTriggerSamples = shortfall.requested_post_trigger_samples;
  outcome.storedPostTriggerSamples = shortfall.stored_post_trigger_samples;
  return outcome;
}

uint64_t timeZeroSample(std::optional<uint64_t> originSampleIndex, std::optional<uint64_t> triggerSampleIndex) {
  if (!originSampleIndex || !triggerSampleIndex || *triggerSampleIndex < *originSampleIndex) return 0;
  return *triggerSampleIndex - *originSampleIndex;
}

uint64_t timeZeroSample(const CaptureOutcome& outcome) {
  return timeZeroSample(outcome.originSampleIndex, outcome.triggerSampleIndex);
}

}
