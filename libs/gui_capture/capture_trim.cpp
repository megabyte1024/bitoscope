/*
 * SPDX-License-Identifier: MIT
 */

#include "capture_trim.h"

#include <format>

namespace bitoscope::gui_capture {

std::optional<bitoscope::capture::CaptureExtent> extentAfterTrim(bitoscope::capture::CaptureExtent extent, TrimSide side,
                                                                 uint64_t at) {
  if (at == 0 || at >= extent.count) return std::nullopt;
  if (side == TrimSide::Before) return bitoscope::capture::CaptureExtent{extent.first_sample + at, extent.count - at};
  return bitoscope::capture::CaptureExtent{extent.first_sample, at};
}

std::expected<CaptureOutcome, std::string> trimmedOutcome(const CaptureOutcome& kept, TrimSide side, uint64_t at) {
  if (!kept.kept()) return std::unexpected(std::string("there is no capture to trim: last run kept nothing"));
  const std::optional<bitoscope::capture::CaptureExtent> extent = extentAfterTrim(kept.extent, side, at);
  if (!extent) {
    return std::unexpected(std::format("trimming at sample {} of {} would delete nothing or every sample", at,
                                       kept.extent.count));
  }
  CaptureOutcome out = kept;
  out.kind = OutcomeKind::Complete;
  out.shortfall.reset();
  out.message.clear();
  out.extent = *extent;
  out.trimmed = true;
  out.requestedSamples = extent->count;
  out.storedSamples = extent->count;
  out.originSampleIndex.reset();
  out.triggerSampleIndex.reset();
  out.requestedPreTriggerSamples.reset();
  out.requestedPostTriggerSamples.reset();
  out.storedPostTriggerSamples.reset();
  if (kept.originSampleIndex && kept.triggerSampleIndex) {
    const uint64_t origin = *kept.originSampleIndex + (extent->first_sample - kept.extent.first_sample);
    const uint64_t trigger = *kept.triggerSampleIndex;
    if (trigger >= origin && trigger - origin < extent->count) {
      const uint64_t pre = trigger - origin;
      out.originSampleIndex = origin;
      out.triggerSampleIndex = trigger;
      out.requestedPreTriggerSamples = pre;
      out.requestedPostTriggerSamples = extent->count - pre;
      out.storedPostTriggerSamples = extent->count - pre;
    }
  }
  return out;
}

}
