/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <span>
#include <vector>

namespace bitoscope::gui_format {

struct PropertyTreeColumns {
  float label = 0.0f;
  float control = 0.0f;
};

PropertyTreeColumns propertyTreeColumns(std::span<const float> labelWidths, float gap,
                                         float availableWidth, float minControlWidth);

int buttonsPerRow(float availableWidth, float buttonSize, float gap);

std::vector<int> buttonRows(std::span<const float> buttonWidths, float gap, float availableWidth);

}
