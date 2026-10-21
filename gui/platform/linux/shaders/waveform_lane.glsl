/*
 * SPDX-License-Identifier: MIT
 */

// WHY: Push constant layout must match LaneConstants in vulkan_waveform_renderer.cpp (128 bytes at most).
// SPIR-V is generated from this file by embed_spirv.py into waveform_spirv.cpp.
layout(push_constant) uniform LaneConstants {
  vec4 band;
  vec4 highColor;
  vec4 lowColor;
  vec4 edgeColor;
  vec4 fillColor;
  vec4 outline;
  vec2 display;
  float lineWidth;
  uint row;
  uint width;
} lane;
