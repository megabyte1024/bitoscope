/*
 * SPDX-License-Identifier: MIT
 */

#include "popup.h"

#include <imgui_internal.h>

namespace bitoscope::gui_popup {

namespace {

// WHY: Seed is not 0 and no window's ID: GetID hashes with the window's ID as seed, so ids here cannot
// collide with it.
constexpr ImGuiID kPopupIdSeed = 0x504F5055u;

}

ImGuiID popupId(std::string_view name) {
  return ImHashStr(name.data(), name.size(), kPopupIdSeed);
}

ImGuiID popupId(std::string_view name, Owner owner) {
  const auto value = static_cast<std::uint64_t>(owner);
  return ImHashStr(name.data(), name.size(), ImHashData(&value, sizeof(value), kPopupIdSeed));
}

bool open(std::string_view name, ImGuiPopupFlags flags) {
  return ImGui::OpenPopupEx(popupId(name), flags);
}

bool open(std::string_view name, Owner owner, ImGuiPopupFlags flags) {
  return ImGui::OpenPopupEx(popupId(name, owner), flags);
}

namespace {

bool beginId(ImGuiID id, ImGuiWindowFlags flags) {
  flags |= ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings;
  return ImGui::BeginPopupEx(id, flags);
}

}

bool begin(std::string_view name, ImGuiWindowFlags flags) {
  return beginId(popupId(name), flags);
}

bool begin(std::string_view name, Owner owner, ImGuiWindowFlags flags) {
  return beginId(popupId(name, owner), flags);
}

bool beginModal(std::string_view name, const char* title, bool* openFlag, ImGuiWindowFlags flags) {
  ImGuiContext& g = *ImGui::GetCurrentContext();
  if (!ImGui::IsPopupOpen(popupId(name), ImGuiPopupFlags_None)) {
    g.NextWindowData.ClearFlags();
    if (openFlag != nullptr && *openFlag) {
      *openFlag = false;
    }
    return false;
  }
  if ((g.NextWindowData.HasFlags & ImGuiNextWindowDataFlags_HasPos) == 0) {
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
  }
  flags |= ImGuiWindowFlags_Popup | ImGuiWindowFlags_Modal | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking;
  const bool isOpen = ImGui::Begin(title, openFlag, flags);
  if (!isOpen || (openFlag != nullptr && !*openFlag)) {
    ImGui::EndPopup();
    if (isOpen) {
      ImGui::ClosePopupToLevel(g.BeginPopupStack.Size, true);
    }
    return false;
  }
  // WHY: Dear ImGui does not close a modal on Escape, so it is handled here.
  if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
    ImGui::CloseCurrentPopup();
  }
  return true;
}

}
