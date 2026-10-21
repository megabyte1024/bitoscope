/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

// WHY: GLFW's Wayland backend never asks the compositor for its cursor theme: without XCURSOR_THEME/SIZE its fallback
// theme has no resize cursors and is small; this decides the values to set before glfwInit().

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace bitoscope::gui_linux {

std::optional<std::string> kcminputrcCursorTheme(std::string_view kcminputrc);

std::optional<int> kcminputrcCursorSize(std::string_view kcminputrc);

bool isKdeDesktop(std::optional<std::string_view> xdgCurrentDesktop);

struct DesktopCursor {
  std::optional<std::string> theme;
  std::optional<int> size;
};
DesktopCursor desktopCursor(std::optional<std::string_view> xdgCurrentDesktop,
                           std::optional<std::string_view> kcminputrc,
                           std::optional<std::string_view> portalTheme,
                           std::optional<std::uint32_t> portalSize);

}
