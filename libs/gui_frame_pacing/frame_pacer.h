/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>

namespace bitoscope::gui_frame_pacing {

enum class WindowVisibility {
  Visible,
  Minimized,
  Occluded,
};

struct FramePacingPolicy {
  std::uint32_t settleMs = 1000;
  std::uint32_t settleFrames = 3;
  std::uint32_t idleTimeoutMs = 500;
  std::uint32_t asyncPollMs = 50;
  std::uint32_t occludedPollMs = 100;
  std::uint32_t minimizedTimeoutMs = 1000;
};

struct FramePacingInput {
  WindowVisibility window = WindowVisibility::Visible;
  bool reduceIdleFrameRate = true;
  bool inputSeen = false;
  bool backgroundChanged = false;
  bool windowChanged = false;
  bool captureRunning = false;
  bool asyncWorkInFlight = false;
  bool uiNeedsMore = false;
};

struct FrameDecision {
  bool render = false;
  bool pumpBackground = true;
  std::uint32_t waitTimeoutMs = 0;
};

class FramePacer {
 public:
  explicit FramePacer(FramePacingPolicy policy = {});

  FrameDecision decide(const FramePacingInput& input, std::uint64_t nowMs);

  void frameRendered();

 private:
  FramePacingPolicy policy_;
  bool haveActivity_ = false;
  std::uint64_t lastActivityMs_ = 0;
  std::uint32_t framesSinceActivity_ = 0;
  WindowVisibility previous_ = WindowVisibility::Minimized;
};

}
