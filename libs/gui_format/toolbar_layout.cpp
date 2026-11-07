/*
 * SPDX-License-Identifier: MIT
 */

#include "toolbar_layout.h"

#include <algorithm>
#include <cmath>

namespace bitoscope::gui_format {

std::vector<std::size_t> toolbarRowLengthChoices(std::size_t channels, std::size_t longestRow) {
  if (channels < 2 || channels > longestRow) {
    return {longestRow};
  }
  return {longestRow, (channels + 1) / 2};
}

std::size_t toolbarLineBreaks(std::span<const float> groupWidths, float availableOnLine, float lineWidth,
                              float groupGap) {
  std::size_t breaks = 0;
  float available = availableOnLine;
  for (const float width : groupWidths) {
    if (available - groupGap < width) {
      ++breaks;
      available = lineWidth - width;
    } else {
      available -= groupGap + width;
    }
  }
  return breaks;
}

ToolbarFit fitToolbar(const ToolbarFitInput& input) {
  const std::size_t choices = (std::max)(std::size_t{1}, input.channelChoices.size());
  const auto breaks = [&](float labelWidth, std::size_t choice) {
    std::vector<float> groups(input.groupsBefore.begin(), input.groupsBefore.end());
    if (!input.channelChoices.empty()) {
      groups.push_back(input.channelChoices[choice]);
    }
    groups.insert(groups.end(), input.groupsAfter.begin(), input.groupsAfter.end());
    return toolbarLineBreaks(groups, input.availableOnLine - labelWidth, input.lineWidth, input.groupGap);
  };
  const float shortest = (std::min)(input.labelMinimum, input.labelWidth);
  std::size_t fewest = breaks(shortest, 0);
  for (std::size_t choice = 1; choice < choices; ++choice) {
    fewest = (std::min)(fewest, breaks(shortest, choice));
  }
  ToolbarFit best{.labelWidth = -1.0f, .channelChoice = 0};
  for (std::size_t choice = 0; choice < choices; ++choice) {
    if (breaks(shortest, choice) > fewest) {
      continue;
    }
    float labelWidth = input.labelWidth;
    if (breaks(labelWidth, choice) > fewest) {
      labelWidth = shortest;
      float low = std::ceil(shortest);
      float high = std::floor(input.labelWidth);
      while (low <= high) {
        const float middle = std::floor((low + high) / 2.0f);
        if (breaks(middle, choice) <= fewest) {
          labelWidth = middle;
          low = middle + 1.0f;
        } else {
          high = middle - 1.0f;
        }
      }
    }
    if (labelWidth > best.labelWidth) {
      best = ToolbarFit{.labelWidth = labelWidth, .channelChoice = choice};
    }
  }
  return best;
}

}
