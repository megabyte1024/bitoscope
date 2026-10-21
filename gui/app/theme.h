/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <imgui.h>

namespace bitoscope::gui {

inline constexpr float kFontSizeBase = 15.0f;

ImGuiStyle makeStyle(bool dark, float dpiScale);

enum class SplitterState { Rest, Hovered, Dragged };

ImVec4 splitterLineColor(const ImGuiStyle& style, SplitterState state);

ImVec4 errorTextColor(bool dark);

void addEdgeCentredOutline(ImDrawList* draw, ImVec2 min, ImVec2 max, ImU32 color, float rounding,
                           float thickness);

}
