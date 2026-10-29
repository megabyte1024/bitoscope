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

  const bool capture = input.captureRunning && policy_.captureFramesPerSecond != 0;
  if (!capture) {
    haveSchedule_ = false;
  }

  if (!input.reduceIdleFrameRate) {
    decision.render = true;
  } else {
    const bool activity = restored || !haveActivity_ || input.inputSeen || input.backgroundChanged ||
                          input.windowChanged || input.captureAdvanced || input.uiNeedsMore;
    if (activity) {
      haveActivity_ = true;
      lastActivityMs_ = nowMs;
      framesSinceActivity_ = 0;
    }
    const std::uint64_t sinceMs = nowMs > lastActivityMs_ ? nowMs - lastActivityMs_ : 0;
    const std::uint32_t settleMs = capture ? policy_.captureSettleMs : policy_.settleMs;
    const bool settling = sinceMs < settleMs || framesSinceActivity_ < policy_.settleFrames;
    const std::uint64_t renderedAgoMs = nowMs > lastRenderMs_ ? nowMs - lastRenderMs_ : 0;
    const bool repaintDue = capture && (!haveRendered_ || renderedAgoMs >= policy_.captureRepaintMs);

    decision.render = activity || settling || repaintDue;
    if (!decision.render) {
      if (capture) {
        decision.waitTimeoutMs = static_cast<std::uint32_t>(policy_.captureRepaintMs - renderedAgoMs);
      } else {
        decision.waitTimeoutMs = policy_.idleTimeoutMs;
      }
      if (input.asyncWorkInFlight) {
        decision.waitTimeoutMs = capture ? (std::min)(decision.waitTimeoutMs, policy_.asyncPollMs)
                                         : policy_.asyncPollMs;
      }
    }
  }

  if (decision.render && capture) {
    const std::uint64_t intervalUs = 1000000 / policy_.captureFramesPerSecond;
    const std::uint64_t nowUs = nowMs * 1000;
    const std::uint64_t earliestUs = nextFrameUs_ > policy_.captureEarlyToleranceMs * 1000ULL
                                       ? nextFrameUs_ - policy_.captureEarlyToleranceMs * 1000ULL
                                       : 0;
    if (haveSchedule_ && nowUs < earliestUs) {
      decision.render = false;
      const std::uint64_t waitUs = nextFrameUs_ - nowUs;
      decision.waitTimeoutMs = static_cast<std::uint32_t>((waitUs + 999) / 1000);
      if (input.asyncWorkInFlight) {
        decision.waitTimeoutMs = (std::min)(decision.waitTimeoutMs, policy_.asyncPollMs);
      }
    } else if (!haveSchedule_ || nowUs >= nextFrameUs_ + intervalUs) {
      haveSchedule_ = true;
      nextFrameUs_ = nowUs + intervalUs;
    } else {
      nextFrameUs_ += intervalUs;
    }
  }
  if (decision.render) {
    haveRendered_ = true;
    lastRenderMs_ = nowMs;
  }
  return decision;
}

void FramePacer::frameRendered() {
  if (framesSinceActivity_ < UINT32_MAX) {
    ++framesSinceActivity_;
  }
}

}
