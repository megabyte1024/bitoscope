/*
 * SPDX-License-Identifier: MIT
 */

#include "waveform_pixel.h"

#include <algorithm>
#include <cmath>

namespace bitoscope::waveform_view {

bool isFullHeight(PixelLevel at, bool hasBefore, PixelLevel before) {
  if (at == PixelLevel::Transition) {
    return true;
  }
  if (!hasBefore) {
    return false;
  }
  return at <= PixelLevel::High && before <= PixelLevel::High && before != at;
}

float lanePixelLineY(PixelLevel here, bool hasOutline, float bandTop, float bandBottom,
                     float lineWidth, float physicalPixel) {
  const float halfWidth = lineWidth * 0.5f;
  const float inset = hasOutline ? physicalPixel : 0.0f;  // WHY: Inset outlined line by one pixel: its outer rim would land one row outside the drawn quad.
  return here == PixelLevel::High ? bandTop + halfWidth + inset : bandBottom - halfWidth - inset;
}

float overlayLineY(PixelLevel here, bool hasOutline, float bandTop, float bandBottom, float lineWidth,
                   float framebufferScale) {
  const float top = std::round(bandTop * framebufferScale);
  const float bottom = std::round(bandBottom * framebufferScale);
  const float width = (std::max)(1.0f, std::round(lineWidth * framebufferScale));
  return lanePixelLineY(here, hasOutline, top, bottom, width, 1.0f) / framebufferScale;
}

PixelPaint classifyLanePixel(const LanePixelInput& in) {
  if (in.here == PixelLevel::NoData) {
    return PixelPaint::Discard;
  }

  const bool fullHeight = isFullHeight(in.here, in.hasBefore, in.before);
  if (fullHeight) {
    if (in.hasOutline && ((in.y - in.bandTop) < 1.0f || (in.bandBottom - in.y) < 1.0f)) {
      return PixelPaint::OutlineRim;
    }
    return PixelPaint::Edge;
  }

  const float lineY = lanePixelLineY(in.here, in.hasOutline, in.bandTop, in.bandBottom, in.lineWidth);
  const float halfWidth = in.lineWidth * 0.5f;
  const float dist = std::abs(in.y - lineY);

  if (in.hasOutline && dist > halfWidth) {
    const bool leftIsFullHeight = in.hasBefore && isFullHeight(in.before, in.hasBefore2, in.before2);
    constexpr bool afterHasBefore = true;
    const bool rightIsFullHeight = in.hasAfter && isFullHeight(in.after, afterHasBefore, in.here);
    if (leftIsFullHeight || rightIsFullHeight) {
      return PixelPaint::OutlineRim;
    }
  }

  if (dist <= halfWidth) {
    return PixelPaint::Line;
  }
  if (in.hasOutline && dist <= halfWidth + 1.0f) {
    return PixelPaint::OutlineRim;
  }
  if (in.here == PixelLevel::High && in.hasFill) {
    return PixelPaint::Fill;
  }
  return PixelPaint::Nothing;
}

}
