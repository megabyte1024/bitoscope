/*
 * SPDX-License-Identifier: MIT
 */

#include "frame_pacer.h"

#include <algorithm>

namespace bitoscope::gui_frame_pacing {

FramePacer::FramePacer(FramePacingPolicy policy) : policy_(policy) {}

FrameDecision FramePacer::decide(const FramePacingInput& input, std::uint64_t nowMs) {
  FrameDecision decision;

  if (input.window != WindowVisibility::Visible) {
    previous_ = input.window;
    decision.render = false;
    decision.waitTimeoutMs = input.window == WindowVisibility::Minimized ? policy_.minimizedTimeoutMs
                                                                         : policy_.occludedPollMs;
    if (input.asyncWorkInFlight) {
      decision.waitTimeoutMs = (std::min)(decision.waitTimeoutMs, policy_.asyncPollMs);
    }
    return decision;
  }

  const bool restored = previous_ != WindowVisibility::Visible;
  previous_ = WindowVisibility::Visible;

  if (!input.reduceIdleFrameRate) {
    decision.render = true;
    return decision;
  }

  const bool activity = restored || !haveActivity_ || input.inputSeen || input.backgroundChanged ||
                        input.windowChanged || input.captureRunning || input.uiNeedsMore;
  if (activity) {
    haveActivity_ = true;
    lastActivityMs_ = nowMs;
    framesSinceActivity_ = 0;
  }
  const std::uint64_t sinceMs = nowMs > lastActivityMs_ ? nowMs - lastActivityMs_ : 0;
  const bool settling = sinceMs < policy_.settleMs || framesSinceActivity_ < policy_.settleFrames;

  decision.render = activity || settling;
  if (!decision.render) {
    decision.waitTimeoutMs = input.asyncWorkInFlight ? policy_.asyncPollMs : policy_.idleTimeoutMs;
  }
  return decision;
}

void FramePacer::frameRendered() {
  if (framesSinceActivity_ < UINT32_MAX) {
    ++framesSinceActivity_;
  }
}

}
