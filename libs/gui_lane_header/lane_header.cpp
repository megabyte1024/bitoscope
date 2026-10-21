/*
 * SPDX-License-Identifier: MIT
 */

#include "lane_header.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include <imgui_internal.h>

namespace bitoscope::gui_lane_header {

bool dragHandle(DragState& state, waveform_view::LaneKey key, const char* id, ImVec2 min, ImVec2 max, float laneTop) {
  ImGui::SetCursorScreenPos(min);
  // WHY: Allow overlap on the drag handle: submitted first, it would keep hover and every press on a button over it.
  ImGui::SetNextItemAllowOverlap();
  ImGui::InvisibleButton(id, ImVec2(max.x - min.x, max.y - min.y));
  if (ImGui::IsItemActivated()) {
    state.candidate = key;
    state.grabOffset = ImGui::GetIO().MousePos.y - laneTop;
  }
  if (ImGui::IsItemActive() && !state.dragged && state.candidate == key && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
    beginLaneDrag(state, key, state.grabOffset);
  }
  return ImGui::IsItemHovered();
}

void beginLaneDrag(DragState& state, waveform_view::LaneKey key, float grabOffset) {
  state.candidate = key;
  state.dragged = key;
  state.grabOffset = grabOffset;
  state.name = ControlPress{};
}

namespace {

// WHY: Clamp the pointer: outside the window it sits at -FLT_MAX, which no integer holds.
waveform_view::PressPoint pressPoint(ImVec2 at) {
  constexpr float kFar = 1.0e9f;
  return {static_cast<std::int32_t>(std::lround(std::clamp(at.x, -kFar, kFar))),
          static_cast<std::int32_t>(std::lround(std::clamp(at.y, -kFar, kFar)))};
}

}

bool headerControl(DragState& state, waveform_view::LaneKey key, float laneTop, bool pressed) {
  ControlPress& press = state.control;
  const ImGuiID item = ImGui::GetItemID();
  const ImGuiIO& io = ImGui::GetIO();
  if (ImGui::IsItemActivated()) {
    press = ControlPress{item, waveform_view::PressTracker(static_cast<std::int32_t>(std::lround(io.MouseDragThreshold))),
                         io.MousePos.y - laneTop};
    press.tracker.press(pressPoint(io.MousePos));
  }
  if (press.item != item) {
    return pressed;
  }
  if (ImGui::IsItemActive()) {
    // WHY: A frame without a valid mouse position is no move, as in Dear ImGui's own drag test.
    if (ImGui::IsMousePosValid() && press.tracker.move(pressPoint(io.MousePos)) == waveform_view::PressState::Drag) {
      ImGui::ClearActiveID();
      beginLaneDrag(state, key, press.grabOffset);
      press = ControlPress{};
      return false;
    }
    return pressed;
  }
  const bool click = press.tracker.release(pressPoint(io.MousePos)) == waveform_view::PressEnd::Click;
  press = ControlPress{};
  return pressed && click;
}

bool nameClicked(DragState& state, ImVec2 min, ImVec2 max) {
  const ImGuiIO& io = ImGui::GetIO();
  const auto inside = [&](ImVec2 at) { return at.x >= min.x && at.x < max.x && at.y >= min.y && at.y < max.y; };
  ControlPress& press = state.name;
  const ImGuiID item = ImGui::GetItemID();
  if (ImGui::IsItemActivated() && inside(io.MousePos)) {
    press = ControlPress{item, waveform_view::PressTracker(static_cast<std::int32_t>(std::lround(io.MouseDragThreshold))),
                         0.0f};
    press.tracker.press(pressPoint(io.MousePos));
  }
  if (press.item != item) {
    return false;
  }
  if (ImGui::IsItemActive()) {
    if (ImGui::IsMousePosValid()) {
      press.tracker.move(pressPoint(io.MousePos));
    }
    return false;
  }
  const bool click = press.tracker.release(pressPoint(io.MousePos)) == waveform_view::PressEnd::Click;
  press = ControlPress{};
  return click && !state.dragged && ImGui::IsItemHovered() && inside(io.MousePos);
}

void beginNameEdit(NameEdit& edit, waveform_view::LaneKey lane, std::string_view current) {
  edit = NameEdit{};
  edit.lane = lane;
  const std::size_t n = (std::min)(current.size(), edit.text.size() - 1);
  std::copy_n(current.begin(), n, edit.text.begin());
  edit.focusPending = true;
}

std::optional<std::string> nameField(NameEdit& edit, const char* id, ImVec2 pos, float width) {
  // WHY: A field that never gets the keyboard would stay open for good: nothing would ever deactivate it.
  constexpr int kFramesToGetFocus = 4;
  ImGui::SetCursorScreenPos(pos);
  ImGui::SetNextItemWidth(width);
  if (edit.focusPending) {
    ImGui::SetKeyboardFocusHere();
    edit.focusPending = false;
  }
  const bool entered = ImGui::InputText(id, edit.text.data(), edit.text.size(),
                                        ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
  const bool left = ImGui::IsItemDeactivated();
  if (entered || (left && !ImGui::IsKeyPressed(ImGuiKey_Escape, false))) {
    std::string typed(edit.text.data());
    edit = NameEdit{};
    return typed;
  }
  if (ImGui::IsItemActive()) {
    edit.framesIdle = 0;
  } else if (left || ++edit.framesIdle > kFramesToGetFocus) {
    edit = NameEdit{};
  }
  return std::nullopt;
}

namespace {

float glyphScale() { return ImGui::GetFontSize() / ImGui::GetFontBaked()->Size; }

const ImFontGlyph& glyphOf(char c) {
  return *ImGui::GetFontBaked()->FindGlyph(static_cast<ImWchar>(static_cast<unsigned char>(c)));
}

}

// WHY: Round the y: Dear ImGui draws text at whole pixels and would cut a fraction off instead.
float capCentredTextY(float top, float height) {
  // WHY: Measure with 'H': it is flat at both ends, while round glyphs overshoot the capital height.
  const ImFontGlyph& cap = glyphOf('H');
  const float inkTop = cap.Y0 * glyphScale();
  const float inkBottom = cap.Y1 * glyphScale();
  return std::round(top + (height - (inkBottom - inkTop)) * 0.5f - inkTop);
}

float inkCentredTextX(std::string_view text, float left, float width) {
  if (text.empty()) {
    return left;
  }
  const float before = ImGui::CalcTextSize(text.data(), text.data() + text.size() - 1).x;
  const float inkLeft = glyphOf(text.front()).X0 * glyphScale();
  const float inkRight = before + glyphOf(text.back()).X1 * glyphScale();
  return std::round(left + (width - (inkRight - inkLeft)) * 0.5f - inkLeft);
}

}
