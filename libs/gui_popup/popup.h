/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

// WHY: Dear ImGui names a popup by the CURRENT window's GetID, so OpenPopup and BeginPopup in different
// windows miss each other.

#include <cstdint>
#include <string_view>

#include <imgui.h>

namespace bitoscope::gui_popup {

ImGuiID popupId(std::string_view name);

enum class Owner : std::uint64_t {};

ImGuiID popupId(std::string_view name, Owner owner);

bool open(std::string_view name, Owner owner, ImGuiPopupFlags flags = ImGuiPopupFlags_None);
bool begin(std::string_view name, Owner owner, ImGuiWindowFlags flags = ImGuiWindowFlags_None);

bool open(std::string_view name, ImGuiPopupFlags flags = ImGuiPopupFlags_None);

bool begin(std::string_view name, ImGuiWindowFlags flags = ImGuiWindowFlags_None);

bool beginModal(std::string_view name, const char* title, bool* openFlag = nullptr,
                ImGuiWindowFlags flags = ImGuiWindowFlags_None);

}
