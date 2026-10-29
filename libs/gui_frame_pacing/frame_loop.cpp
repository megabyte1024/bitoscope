/*
 * SPDX-License-Identifier: MIT
 */

#include "frame_loop.h"

namespace bitoscope::gui_frame_pacing {

WindowVisibility windowVisibility(bool iconified, bool occluded, int framebufferWidth, int framebufferHeight) {
  if (iconified || framebufferWidth <= 0 || framebufferHeight <= 0) {
    return WindowVisibility::Minimized;
  }
  if (occluded) {
    return WindowVisibility::Occluded;
  }
  return WindowVisibility::Visible;
}

FramePacingInput framePacingInput(const WindowObservation& window, const AppObservation& app, int lastDrawnWidth,
                                  int lastDrawnHeight) {
  FramePacingInput input;
  input.window = windowVisibility(window.iconified, window.occluded, window.framebufferWidth, window.framebufferHeight);
  input.reduceIdleFrameRate = app.reduceIdleFrameRate;
  input.inputSeen = window.eventsHandled || window.testHookBusy;
  input.backgroundChanged = app.backgroundChanged;
  input.windowChanged = window.changePending || window.framebufferWidth != lastDrawnWidth ||
                        window.framebufferHeight != lastDrawnHeight;
  input.captureRunning = app.captureRunning;
  input.captureAdvanced = app.captureAdvanced;
  input.asyncWorkInFlight = app.asyncWorkInFlight;
  input.uiNeedsMore = app.uiNeedsMore;
  return input;
}

int runFrameLoop(const FrameLoopHooks& hooks, FramePacer& pacer) {
  int lastDrawnWidth = 0;
  int lastDrawnHeight = 0;
  for (;;) {
    hooks.handleEvents();
    if (hooks.closeRequested()) {
      return 0;
    }
    const WindowObservation window = hooks.observeWindow();
    // WHY: Pump the background in every window state, minimized included.
    const AppObservation app = hooks.pumpBackground();
    if (hooks.failed()) {
      return 1;
    }
    const FrameDecision decision =
      pacer.decide(framePacingInput(window, app, lastDrawnWidth, lastDrawnHeight), hooks.nowMs());
    if (decision.render) {
      hooks.drawFrame();
      if (hooks.failed()) {
        return 1;
      }
      pacer.frameRendered();
      lastDrawnWidth = window.framebufferWidth;
      lastDrawnHeight = window.framebufferHeight;
      continue;
    }
    hooks.wait(decision.waitTimeoutMs);
  }
}

}
