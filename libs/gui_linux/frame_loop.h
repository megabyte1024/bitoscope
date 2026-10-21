/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

// WHY: GLFW's Wayland backend always reports not iconified, so a minimized Wayland window counts as Visible;
// Occluded is never reported because GLFW has no occlusion query.

#include <frame_pacer.h>

#include <cstdint>
#include <functional>

namespace bitoscope::gui_linux {

struct WindowObservation {
  bool iconified = false;
  int framebufferWidth = 0;
  int framebufferHeight = 0;
  bool eventsHandled = false;
  bool changePending = false;
  bool testHookBusy = false;
};

struct AppObservation {
  bool backgroundChanged = false;
  bool captureRunning = false;
  bool asyncWorkInFlight = false;
  bool uiNeedsMore = false;
  bool reduceIdleFrameRate = false;
};

gui_frame_pacing::WindowVisibility windowVisibility(bool iconified, int framebufferWidth, int framebufferHeight);

gui_frame_pacing::FramePacingInput framePacingInput(const WindowObservation& window, const AppObservation& app,
                                                    int lastDrawnWidth, int lastDrawnHeight);

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

int runFrameLoop(const FrameLoopHooks& hooks, gui_frame_pacing::FramePacer& pacer);

}
