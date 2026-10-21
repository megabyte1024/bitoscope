/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <vector>

namespace bitoscope::gui_session {

enum class PropertiesColumn {
  Bar,
  Pane,
  Splitter,
  Waveform,
};

std::vector<PropertiesColumn> propertiesLayout(bool onLeft, bool open);

struct SplitterDrag {
  bool onLeft = true;
  float bodyLeft = 0.0f;
  float bodyRight = 0.0f;
  float barWidth = 0.0f;
  float splitterWidth = 0.0f;
  float narrowest = 0.0f;
  float widest = 0.0f;
  float grabOffset = 0.0f;
};

float propertiesWidthAtPointer(const SplitterDrag& drag, float pointerX);

float centredOffset(float columnWidth, float itemWidth);

}
