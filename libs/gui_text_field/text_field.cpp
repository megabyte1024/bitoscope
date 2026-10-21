/*
 * SPDX-License-Identifier: MIT
 */

#include "text_field.h"

namespace bitoscope::gui_text_field {

Commit committedInputText(const char* label, char* buffer, std::size_t size, ImGuiInputTextFlags flags) {
  if (ImGui::InputText(label, buffer, size, flags | ImGuiInputTextFlags_EnterReturnsTrue)) {
    return Commit::Enter;
  }
  // WHY: Dear ImGui counts Esc as an edit (it writes the old text back), so tell it apart by its key.
  if (ImGui::IsItemDeactivatedAfterEdit() && !ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
    return Commit::FocusLeft;
  }
  return Commit::None;
}

}
