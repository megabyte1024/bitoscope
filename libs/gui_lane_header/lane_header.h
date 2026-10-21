/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <array>
#include <optional>
#include <string>
#include <string_view>

#include <imgui.h>

#include <header_press.h>
#include <lane_order.h>

namespace bitoscope::gui_lane_header {

struct ControlPress {
  ImGuiID item = 0;
  waveform_view::PressTracker tracker{1};
  float grabOffset = 0.0f;
};

struct DragState {
  std::optional<waveform_view::LaneKey> candidate;
  std::optional<waveform_view::LaneKey> dragged;
  float grabOffset = 0.0f;
  ControlPress control;
  ControlPress name;
};

bool dragHandle(DragState& state, waveform_view::LaneKey key, const char* id, ImVec2 min, ImVec2 max, float laneTop);

void beginLaneDrag(DragState& state, waveform_view::LaneKey key, float grabOffset);

bool headerControl(DragState& state, waveform_view::LaneKey key, float laneTop, bool pressed);

struct NameEdit {
  std::optional<waveform_view::LaneKey> lane;
  std::array<char, 64> text{};
  bool focusPending = false;
  int framesIdle = 0;
};

bool nameClicked(DragState& state, ImVec2 min, ImVec2 max);

void beginNameEdit(NameEdit& edit, waveform_view::LaneKey lane, std::string_view current);

std::optional<std::string> nameField(NameEdit& edit, const char* id, ImVec2 pos, float width);

float capCentredTextY(float top, float height);

float inkCentredTextX(std::string_view text, float left, float width);

}
