/*
 * SPDX-License-Identifier: MIT
 */

#include "window_system.h"

namespace bitoscope::gui_linux {

GlfwWindowSystem expectedGlfwWindowSystem(const WindowSystemEnvironment& environment) {
  if (environment.xdgSessionType == "wayland" && environment.waylandDisplay.has_value()) {
    return GlfwWindowSystem::Wayland;
  }
  if (environment.xdgSessionType == "x11" && environment.display.has_value()) {
    return GlfwWindowSystem::X11;
  }
  // WHY: GLFW tries Wayland first; whether it connects cannot be known without trying, so this guesses.
  if (environment.waylandDisplay.has_value() && !environment.waylandDisplay->empty()) {
    return GlfwWindowSystem::Wayland;
  }
  return GlfwWindowSystem::X11;
}

}
