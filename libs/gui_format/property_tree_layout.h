/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <span>

namespace bitoscope::gui_format {

struct PropertyTreeColumns {
  float label = 0.0f;
  float control = 0.0f;
};

PropertyTreeColumns propertyTreeColumns(std::span<const float> labelWidths, float gap,
                                         float availableWidth, float minControlWidth);

int buttonsPerRow(float availableWidth, float buttonSize, float gap);

}
