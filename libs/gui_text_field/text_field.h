/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

// WHY: Owner decision: every value editor applies an edit by Enter or by leaving focus.

#include <cstddef>

#include <imgui.h>

namespace bitoscope::gui_text_field {

enum class Commit {
  None,
  Enter,
  FocusLeft,
};

Commit committedInputText(const char* label, char* buffer, std::size_t size, ImGuiInputTextFlags flags = 0);

}
