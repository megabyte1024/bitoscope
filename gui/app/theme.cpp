/*
 * SPDX-License-Identifier: MIT
 */

#include "theme.h"

namespace bitoscope::gui {

namespace {

struct Palette {
  ImVec4 window;
  ImVec4 sunken;
  ImVec4 surface;
  ImVec4 frame;
  ImVec4 hover;
  ImVec4 pressed;
  ImVec4 border;
  ImVec4 text;
  ImVec4 muted;
  ImVec4 accent;
  ImVec4 accentHover;
  float accentSoftAlpha;
};

constexpr ImVec4 rgb(int r, int g, int b, float a = 1.0f) {
  return ImVec4(r / 255.0f, g / 255.0f, b / 255.0f, a);
}

constexpr ImVec4 withAlpha(ImVec4 c, float a) {
  return ImVec4(c.x, c.y, c.z, a);
}

constexpr Palette kDark{
  .window = rgb(31, 33, 36),
  .sunken = rgb(24, 25, 27),
  .surface = rgb(38, 40, 44),
  .frame = rgb(44, 47, 52),
  .hover = rgb(54, 58, 64),
  .pressed = rgb(64, 69, 76),
  .border = rgb(58, 62, 68),
  .text = rgb(227, 229, 232),
  .muted = rgb(139, 144, 152),
  .accent = rgb(76, 141, 246),
  .accentHover = rgb(110, 164, 248),
  .accentSoftAlpha = 0.30f,
};

constexpr Palette kLight{
  .window = rgb(246, 247, 249),
  .sunken = rgb(232, 234, 237),
  .surface = rgb(255, 255, 255),
  .frame = rgb(226, 229, 233),
  .hover = rgb(214, 218, 224),
  .pressed = rgb(200, 206, 214),
  .border = rgb(204, 209, 215),
  .text = rgb(29, 33, 39),
  .muted = rgb(112, 119, 129),
  .accent = rgb(31, 111, 235),
  .accentHover = rgb(59, 130, 240),
  .accentSoftAlpha = 0.20f,
};

void applyPalette(ImGuiStyle& style, const Palette& p, bool dark) {
  ImVec4* c = style.Colors;
  const ImVec4 none(0, 0, 0, 0);
  const ImVec4 accentSoft = withAlpha(p.accent, p.accentSoftAlpha);

  c[ImGuiCol_Text] = p.text;
  c[ImGuiCol_TextDisabled] = p.muted;
  c[ImGuiCol_WindowBg] = p.window;
  c[ImGuiCol_ChildBg] = none;
  c[ImGuiCol_PopupBg] = p.surface;
  c[ImGuiCol_Border] = p.border;
  c[ImGuiCol_BorderShadow] = none;
  c[ImGuiCol_FrameBg] = p.frame;
  c[ImGuiCol_FrameBgHovered] = p.hover;
  c[ImGuiCol_FrameBgActive] = p.pressed;
  c[ImGuiCol_TitleBg] = p.sunken;
  c[ImGuiCol_TitleBgActive] = p.sunken;
  c[ImGuiCol_TitleBgCollapsed] = p.sunken;
  c[ImGuiCol_MenuBarBg] = p.sunken;
  c[ImGuiCol_ScrollbarBg] = none;
  c[ImGuiCol_ScrollbarGrab] = p.pressed;
  c[ImGuiCol_ScrollbarGrabHovered] = withAlpha(p.muted, 0.7f);
  c[ImGuiCol_ScrollbarGrabActive] = p.muted;
  c[ImGuiCol_CheckMark] = p.accent;
  c[ImGuiCol_CheckboxSelectedBg] = p.frame;
  c[ImGuiCol_SliderGrab] = p.accent;
  c[ImGuiCol_SliderGrabActive] = p.accentHover;
  c[ImGuiCol_Button] = p.frame;
  c[ImGuiCol_ButtonHovered] = p.hover;
  c[ImGuiCol_ButtonActive] = p.pressed;
  c[ImGuiCol_Header] = accentSoft;
  c[ImGuiCol_HeaderHovered] = p.hover;
  c[ImGuiCol_HeaderActive] = withAlpha(p.accent, p.accentSoftAlpha + 0.15f);
  c[ImGuiCol_Separator] = p.border;
  c[ImGuiCol_SeparatorHovered] = withAlpha(p.accent, 0.6f);
  c[ImGuiCol_SeparatorActive] = p.accent;
  c[ImGuiCol_ResizeGrip] = withAlpha(p.border, 0.5f);
  c[ImGuiCol_ResizeGripHovered] = withAlpha(p.accent, 0.6f);
  c[ImGuiCol_ResizeGripActive] = p.accent;
  c[ImGuiCol_InputTextCursor] = p.text;
  c[ImGuiCol_TabHovered] = p.hover;
  c[ImGuiCol_Tab] = p.sunken;
  c[ImGuiCol_TabSelected] = p.window;
  c[ImGuiCol_TabSelectedOverline] = p.accent;
  c[ImGuiCol_TabDimmed] = p.sunken;
  c[ImGuiCol_TabDimmedSelected] = p.window;
  // WHY: Grey without keyboard focus on the tab bar: blue says where the keys go (owner decision).
  c[ImGuiCol_TabDimmedSelectedOverline] = p.border;
  c[ImGuiCol_DockingPreview] = withAlpha(p.accent, 0.5f);
  c[ImGuiCol_DockingEmptyBg] = p.sunken;
  c[ImGuiCol_PlotLines] = p.accent;
  c[ImGuiCol_PlotLinesHovered] = p.accentHover;
  c[ImGuiCol_PlotHistogram] = p.accent;
  c[ImGuiCol_PlotHistogramHovered] = p.accentHover;
  c[ImGuiCol_TableHeaderBg] = p.sunken;
  c[ImGuiCol_TableBorderStrong] = p.border;
  c[ImGuiCol_TableBorderLight] = withAlpha(p.border, 0.6f);
  c[ImGuiCol_TableRowBg] = none;
  c[ImGuiCol_TableRowBgAlt] = withAlpha(p.text, 0.035f);
  c[ImGuiCol_TextLink] = dark ? p.accentHover : p.accent;
  c[ImGuiCol_TextSelectedBg] = withAlpha(p.accent, 0.35f);
  c[ImGuiCol_TreeLines] = p.border;
  c[ImGuiCol_DragDropTarget] = p.accent;
  c[ImGuiCol_DragDropTargetBg] = withAlpha(p.accent, 0.10f);
  c[ImGuiCol_UnsavedMarker] = p.text;
  c[ImGuiCol_NavCursor] = p.accent;
  c[ImGuiCol_NavWindowingHighlight] = withAlpha(p.text, 0.7f);
  c[ImGuiCol_NavWindowingDimBg] = ImVec4(0, 0, 0, 0.20f);
  c[ImGuiCol_ModalWindowDimBg] = ImVec4(0, 0, 0, dark ? 0.45f : 0.30f);
  static_assert(ImGuiCol_COUNT == 63,
                "Dear ImGui added or removed a style color: give it a palette token here");
}

}

ImVec4 errorTextColor(bool dark) {
  return dark ? rgb(242, 109, 97) : rgb(196, 43, 28);
}

ImVec4 splitterLineColor(const ImGuiStyle& style, SplitterState state) {
  // WHY: Not ImGuiCol_Separator (1.4:1 on the window): a splitter grab line must hold 3:1 contrast.
  if (state == SplitterState::Rest) return style.Colors[ImGuiCol_TextDisabled];
  return style.Colors[ImGuiCol_SeparatorActive];
}

ImGuiStyle makeStyle(bool dark, float dpiScale) {
  ImGuiStyle style;

  style.WindowPadding = ImVec2(10, 10);
  style.FramePadding = ImVec2(8, 5);
  style.CellPadding = ImVec2(6, 4);
  style.ItemSpacing = ImVec2(8, 6);
  style.ItemInnerSpacing = ImVec2(6, 4);
  style.IndentSpacing = 18;
  style.ScrollbarSize = 12;
  style.GrabMinSize = 10;
  style.WindowBorderSize = 1;
  style.ChildBorderSize = 1;
  style.PopupBorderSize = 1;
  style.FrameBorderSize = 0;
  style.TabBorderSize = 0;
  style.WindowRounding = 4;
  style.ChildRounding = 4;
  style.FrameRounding = 4;
  style.PopupRounding = 4;
  style.ScrollbarRounding = 6;
  style.GrabRounding = 4;
  style.TabRounding = 4;
  style.TabBarOverlineSize = 2;
  style.WindowTitleAlign = ImVec2(0.0f, 0.5f);
  style.WindowMenuButtonPosition = ImGuiDir_None;

  applyPalette(style, dark ? kDark : kLight, dark);

  // WHY: ScaleAllSizes multiplies whatever the style holds, so apply it once, to this fresh style.
  style.ScaleAllSizes(dpiScale);
  style.FontSizeBase = kFontSizeBase;
  style.FontScaleDpi = dpiScale;
  return style;
}

void addEdgeCentredOutline(ImDrawList* draw, ImVec2 min, ImVec2 max, ImU32 color, float rounding,
                           float thickness) {
  // WHY: Centred on the edge pixels, not inside: a 2-unit outline straddles the box edge, as chips show.
  draw->AddRect(ImVec2(min.x + 0.5f, min.y + 0.5f), ImVec2(max.x - 0.5f, max.y - 0.5f), color, rounding, thickness,
                ImDrawFlags_StrokeCenter);
}

}
