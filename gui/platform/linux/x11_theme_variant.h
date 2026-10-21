/*
 * SPDX-License-Identifier: MIT
 */

// WHY: Kept apart from linux_platform.cpp: Xlib's headers define macros (None, Success, Bool,
// Status) that no other file may see.
#pragma once

#include <cstdint>

struct GLFWwindow;

namespace bitoscope::gui::linux_platform {

void setX11ThemeVariant(GLFWwindow* window, bool dark);

std::uint64_t x11WindowId(GLFWwindow* window);

}
