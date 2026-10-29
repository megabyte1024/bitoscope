/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "frame_pacer.h"

#include <cstdint>
#include <functional>

namespace bitoscope::gui_frame_pacing {

struct WindowObservation {
  bool iconified = false;
  bool occluded = false;
  int framebufferWidth = 0;
  int framebufferHeight = 0;
  bool eventsHandled = false;
  bool changePending = false;
  bool testHookBusy = false;
};

struct AppObservation {
  bool backgroundChanged = false;
  bool captureRunning = false;
  bool captureAdvanced = false;
  bool asyncWorkInFlight = false;
  bool uiNeedsMore = false;
  bool reduceIdleFrameRate = false;
};

WindowVisibility windowVisibility(bool iconified, bool occluded, int framebufferWidth, int framebufferHeight);

FramePacingInput framePacingInput(const WindowObservation& window, const AppObservation& app, int lastDrawnWidth,
                                  int lastDrawnHeight);

struct FrameLoopHooks {
  std::function<void()> handleEvents;
  std::function<bool()> closeRequested;
  std::function<WindowObservation()> observeWindow;
  std::function<AppObservation()> pumpBackground;
  std::function<void()> drawFrame;
  std::function<bool()> failed;
  std::function<void(std::uint32_t timeoutMs)> wait;
  std::function<std::uint64_t()> nowMs;
};

int runFrameLoop(const FrameLoopHooks& hooks, FramePacer& pacer);

}
