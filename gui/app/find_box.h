/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <imgui.h>

#include <decoder.h>
#include <find_pattern.h>
#include <find_search.h>

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>

namespace bitoscope::gui {

inline constexpr int kFindFocusFrames = 4;

struct FindBox {
  bool open = false;
  std::array<char, 1024> text{};
  gui_decode::FindMode mode = gui_decode::FindMode::Ascii;
  bool matchCase = false;
  bool notPrintable = false;
  bool stepped = false;
  bool fieldActive = false;
  bool selectAll = false;
  int focusFrames = 0;
  float width = 0.0f;
  std::unique_ptr<gui_decode::FindSearch> search;

  bool ownsKeys() const { return open && (fieldActive || focusFrames > 0); }
};

struct FindMarkLane {
  std::uint32_t decoder = 0;
  std::span<const decode::AnnotationRow> rows;
  float top = 0.0f;
  float rowPitch = 0.0f;
  ImVec2 clipMin;
  ImVec2 clipMax;
  std::uint64_t firstSample = 0;
  std::uint64_t endSample = 0;
  double samplesPerPixel = 1.0;
  std::function<float(std::uint64_t)> sampleX;
  ImU32 color = 0;
  float frameWidth = 1.0f;
  float minWidth = 1.0f;
};

void drawFindMarks(ImDrawList* draw, const FindBox& find, const FindMarkLane& lane);

void switchFindMode(FindBox& find, gui_decode::FindMode to);

}
