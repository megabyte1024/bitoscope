/*
 * SPDX-License-Identifier: MIT
 */

#include "panel_decodes.h"

#include <cstdint>
#include <utility>

#include "ring_config.h"

#include "decode_signal_view.h"
#include "growing_capture.h"

namespace bitoscope::gui_decode {

namespace gc = bitoscope::gui_capture;

LiveJobAtEnd liveJobAtEnd(gc::OutcomeKind kind) {
  switch (kind) {
    case gc::OutcomeKind::Complete:
    case gc::OutcomeKind::ShortMemory:
    case gc::OutcomeKind::ShortStoppedByUser:
    case gc::OutcomeKind::ShortStoppedWithSession:
      return LiveJobAtEnd::Keep;
    case gc::OutcomeKind::Failed:
      return LiveJobAtEnd::Cancel;
  }
  return LiveJobAtEnd::Cancel;
}

bool liveDecodingAllowed(const gc::CaptureRequest& request) {
  const auto channels = static_cast<std::uint32_t>(request.deviceChannels.size());
  return bitoscope::acquisition::captureBytesPerSecond(channels, request.request.sample_rate_milli_hz) <=
         kLiveDecodeMaxBytesPerSecond;
}

std::span<const std::uint32_t> settingsChannels(const std::optional<gc::CaptureRequest>& running,
                                                std::span<const std::uint32_t> kept) {
  return running ? std::span<const std::uint32_t>(running->deviceChannels) : kept;
}

void PanelDecodes::run(DecodeRun run) {
  cancel();
  run_ = std::move(run);
}

void PanelDecodes::cancel() {
  run_ = DecodeRun{};
  live_.reset();
  tried_.reset();
}

LiveStep PanelDecodes::follow(const LiveAttempt& attempt, bool allowed, const StartLive& start) {
  if (!attempt.session) return LiveStep::None;
  LiveStep step = LiveStep::None;
  if (live_ && live_->generation != attempt.generation) {
    cancel();
    step = LiveStep::Cancelled;
  }
  if (live_ || tried_ == attempt.generation || !allowed) return step;
  if (attempt.session->snapshot().extent.count == 0) return step;
  tried_ = attempt.generation;
  auto capture = std::make_shared<LiveRunCapture>(decoder_host::sessionCapture(attempt.session), attempt.boundSamples);
  DecodeRun started = start(capture);
  if (!started.runner) return step;
  run_ = std::move(started);
  live_ = Live{attempt.generation, attempt.session, std::move(capture)};
  return LiveStep::Started;
}

bool PanelDecodes::captureEnded(const gc::CaptureOutcome& outcome) {
  if (!live_) return false;
  if (liveJobAtEnd(outcome.kind) == LiveJobAtEnd::Cancel || outcome.session != live_->session) {
    cancel();
    return false;
  }
  auto finished = runCaptureForOutcome(outcome);
  auto views = signalViewSourceForOutcome(outcome);
  if (!finished || !views) {
    cancel();
    return false;
  }
  live_->endedShort = finished->extent.count < live_->capture->boundSamples();
  live_->capture->keep(std::move(*finished), std::move(*views));
  return true;
}

bool PanelDecodes::decodeAgainAfterCapture(std::uint64_t run, std::size_t position) const {
  if (!live_ || run != run_.id) return false;
  return (live_->endedShort && run_.runner->readsGrowingCapture(position)) || run_.runner->ranOutOfBudget(position);
}

bitoscope::waveform_view::LaneCaptureMode PanelDecodes::laneCaptureMode(bool captureRunning, bool gateAllowed,
                                                                        std::optional<std::size_t> runPosition) const {
  using bitoscope::waveform_view::LaneCaptureMode;
  if (!captureRunning) return LaneCaptureMode::Ended;
  if (live_ && runPosition && run_.runner->readsGrowingCapture(*runPosition)) return LaneCaptureMode::Live;
  if (!gateAllowed || tried_) return LaneCaptureMode::StartsAtEnd;
  return LaneCaptureMode::Ended;
}

}
