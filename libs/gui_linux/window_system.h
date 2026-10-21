/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

// WHY: Decide the window system before glfwInit(): XCURSOR_* would change X11 cursors too, so apply them
// only on Wayland.

#include <optional>
#include <string_view>

namespace bitoscope::gui_linux {

enum class GlfwWindowSystem {
  Wayland,
  X11,
};

struct WindowSystemEnvironment {
  std::optional<std::string_view> xdgSessionType;
  std::optional<std::string_view> waylandDisplay;
  std::optional<std::string_view> display;
};

GlfwWindowSystem expectedGlfwWindowSystem(const WindowSystemEnvironment& environment);

}
