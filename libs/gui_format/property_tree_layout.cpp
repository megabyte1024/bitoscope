/*
 * SPDX-License-Identifier: MIT
 */

#include "property_tree_layout.h"

#include <algorithm>
#include <cmath>

#include <algorithm>

namespace bitoscope::gui_format {

PropertyTreeColumns propertyTreeColumns(std::span<const float> labelWidths, float gap,
                                         float availableWidth, float minControlWidth) {
  float widest = 0.0f;
  for (float width : labelWidths) {
    widest = std::max(widest, width);
  }
  float label = labelWidths.empty() ? 0.0f : widest + gap;
  label = std::max(0.0f, label);
  const float maxLabel = std::max(0.0f, availableWidth - minControlWidth);
  label = std::min(label, maxLabel);
  const float control = std::max(0.0f, availableWidth - label);
  return PropertyTreeColumns{label, control};
}

int buttonsPerRow(float availableWidth, float buttonSize, float gap) {
  const float step = buttonSize + gap;
  if (step <= 0.0f || availableWidth <= 0.0f) return 1;
  const int n = static_cast<int>(std::floor((availableWidth + gap) / step));
  return (std::max)(1, n);
}

std::vector<int> buttonRows(std::span<const float> buttonWidths, float gap, float availableWidth) {
  std::vector<int> rows;
  rows.reserve(buttonWidths.size());
  int row = 0;
  float rowWidth = 0.0f;
  for (const float width : buttonWidths) {
    if (rows.empty()) {
      rowWidth = width;
    } else if (rowWidth + gap + width > availableWidth) {
      ++row;
      rowWidth = width;
    } else {
      rowWidth += gap + width;
    }
    rows.push_back(row);
  }
  return rows;
}

}
