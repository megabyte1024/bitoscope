/*
 * SPDX-License-Identifier: MIT
 */

#version 450
// WHY: Vulkan clip space has y pointing down.
#extension GL_GOOGLE_include_directive : require
#include "waveform_lane.glsl"

void main() {
  vec2 corner = vec2(float(gl_VertexIndex & 1), float(gl_VertexIndex >> 1));
  vec2 p = lane.band.xy + (lane.band.zw - lane.band.xy) * corner;
  gl_Position = vec4(p.x / lane.display.x * 2.0 - 1.0, p.y / lane.display.y * 2.0 - 1.0, 0.0, 1.0);
}
