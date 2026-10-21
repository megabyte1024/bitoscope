/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

// WHY: Mirrored line for line by waveform.frag and d3d11 ps_main: change all three together.

#include <cstdint>

namespace bitoscope::waveform_view {

enum class PixelLevel : std::uint8_t {
  Low = 0,
  High = 1,
  Transition = 2,
  NoData = 3,
};

enum class PixelPaint {
  Discard,
  Line,
  OutlineRim,
  Edge,
  Fill,
  Nothing,
};

bool isFullHeight(PixelLevel at, bool hasBefore, PixelLevel before);

float lanePixelLineY(PixelLevel here, bool hasOutline, float bandTop, float bandBottom,
                     float lineWidth, float physicalPixel = 1.0f);

float overlayLineY(PixelLevel here, bool hasOutline, float bandTop, float bandBottom, float lineWidth,
                   float framebufferScale);

struct LanePixelInput {
  PixelLevel here = PixelLevel::NoData;

  bool hasBefore = false;
  PixelLevel before = PixelLevel::NoData;

  bool hasBefore2 = false;
  PixelLevel before2 = PixelLevel::NoData;

  bool hasAfter = false;
  PixelLevel after = PixelLevel::NoData;

  bool hasOutline = false;
  bool hasFill = false;

  float y = 0.0f;
  float bandTop = 0.0f;
  float bandBottom = 0.0f;
  float lineWidth = 1.0f;
};

PixelPaint classifyLanePixel(const LanePixelInput& in);

}
