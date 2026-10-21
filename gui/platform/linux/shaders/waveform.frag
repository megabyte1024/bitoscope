/*
 * SPDX-License-Identifier: MIT
 */

#version 450
// WHY: Per-pixel decision is hand-transcribed from classifyLanePixel() in waveform_pixel.h: keep both in step.
// Fill is blended with a real alpha, so the pipeline enables standard alpha blending.
#extension GL_GOOGLE_include_directive : require
#include "waveform_lane.glsl"

layout(std430, set = 0, binding = 0) readonly buffer Levels {
  uint words[];
} levels;

layout(location = 0) out vec4 outColor;

uint levelAt(int column) {
  uint i = lane.row * lane.width + uint(column);
  return (levels.words[i >> 2] >> ((i & 3u) * 8u)) & 0xFFu;
}

bool isFullHeightAt(uint at, bool hasBefore, uint before) {
  if (at == 2u) {
    return true;
  }
  if (!hasBefore) {
    return false;
  }
  return at <= 1u && before <= 1u && before != at;
}

void main() {
  int col = int(floor(gl_FragCoord.x - lane.band.x));
  if (col < 0 || col >= int(lane.width)) {
    discard;
  }
  uint here = levelAt(col);
  if (here == 3u) {
    discard;
  }
  bool hasOutline = lane.outline.a > 0.5;

  bool hasBefore = col > 0;
  uint before = hasBefore ? levelAt(col - 1) : 0u;
  bool fullHeight = isFullHeightAt(here, hasBefore, before);
  if (fullHeight) {
    if (hasOutline && ((gl_FragCoord.y - lane.band.y) < 1.0 || (lane.band.w - gl_FragCoord.y) < 1.0)) {
      outColor = vec4(lane.outline.rgb, 1.0);
      return;
    }
    outColor = lane.edgeColor;
    return;
  }

  // WHY: An outlined lane's line sits one pixel inward so its outer rim lands inside the quad and is rasterized.
  float halfWidth = lane.lineWidth * 0.5;
  float inset = hasOutline ? 1.0 : 0.0;
  float lineY = here == 1u ? lane.band.y + halfWidth + inset : lane.band.w - halfWidth - inset;
  vec4 lineColor = here == 1u ? lane.highColor : lane.lowColor;
  float dist = abs(gl_FragCoord.y - lineY);

  // WHY: A column beside a full-height one draws its non-line pixels as the vertical's side rim, over its whole height.
  if (hasOutline && dist > halfWidth) {
    bool hasBefore2 = col > 1;
    uint before2 = hasBefore2 ? levelAt(col - 2) : 0u;
    bool leftIsFullHeight = hasBefore && isFullHeightAt(before, hasBefore2, before2);
    bool hasAfter = col + 1 < int(lane.width);
    uint after = hasAfter ? levelAt(col + 1) : 0u;
    bool rightIsFullHeight = hasAfter && isFullHeightAt(after, true, here);
    if (leftIsFullHeight || rightIsFullHeight) {
      outColor = vec4(lane.outline.rgb, 1.0);
      return;
    }
  }

  if (dist <= halfWidth) {
    outColor = lineColor;
    return;
  }
  if (hasOutline && dist <= halfWidth + 1.0) {
    outColor = vec4(lane.outline.rgb, 1.0);
    return;
  }
  if (here == 1u && lane.fillColor.a > 0.0) {
    outColor = lane.fillColor;
    return;
  }
  discard;
}
