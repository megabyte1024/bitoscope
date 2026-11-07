/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstddef>
#include <span>
#include <vector>

namespace bitoscope::gui_format {

std::vector<std::size_t> toolbarRowLengthChoices(std::size_t channels, std::size_t longestRow);

std::size_t toolbarLineBreaks(std::span<const float> groupWidths, float availableOnLine, float lineWidth,
                              float groupGap);

struct ToolbarFitInput {
  float availableOnLine = 0.0f;
  float lineWidth = 0.0f;
  float groupGap = 0.0f;
  float labelWidth = 0.0f;
  float labelMinimum = 0.0f;
  std::span<const float> groupsBefore;
  std::span<const float> channelChoices;
  std::span<const float> groupsAfter;
};

struct ToolbarFit {
  float labelWidth = 0.0f;
  std::size_t channelChoice = 0;
};

ToolbarFit fitToolbar(const ToolbarFitInput& input);

}
