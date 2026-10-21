/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

// WHY: No icon on Wayland: GLFW 3.5 cannot set it there, and a desktop file registered with the shell is postponed.

#include <filesystem>
#include <string>
#include <vector>

struct GLFWwindow;

namespace bitoscope::gui::linux_platform {

std::vector<std::string> setX11WindowIcon(GLFWwindow* window, const std::filesystem::path& assetDir);

}
