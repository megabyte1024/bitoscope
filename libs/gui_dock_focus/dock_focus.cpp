/*
 * SPDX-License-Identifier: MIT
 */

#include "dock_focus.h"

#include <imgui.h>
#include <imgui_internal.h>

namespace bitoscope::gui_dock_focus {

void bringToFrontBeforeBegin(const char* windowName) {
  ImGui::SetNextWindowFocus();
  if (const ImGuiWindow* window = ImGui::FindWindowByName(windowName);
      window != nullptr && window->DockNode != nullptr && window->DockNode->TabBar != nullptr) {
    window->DockNode->TabBar->NextSelectedTabId = window->TabId;
  }
}

}
