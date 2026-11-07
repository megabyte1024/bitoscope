/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstddef>
#include <optional>
#include <vector>

namespace bitoscope::gui_format {

struct ChannelGridSpec {
  std::size_t perRow = 1;
  std::size_t groupLength = 0;
  float tileWidth = 0.0f;
  float tileHeight = 0.0f;
  float gapX = 0.0f;
  float gapY = 0.0f;
  float groupGap = 0.0f;
};

struct ChannelGridCell {
  float x = 0.0f;
  float y = 0.0f;
};

struct ChannelGrid {
  std::vector<ChannelGridCell> cells;
  float tileWidth = 0.0f;
  float tileHeight = 0.0f;
  float width = 0.0f;
  float height = 0.0f;
};

ChannelGrid channelGrid(std::size_t channels, const ChannelGridSpec& spec);

std::optional<std::size_t> channelGridCellAt(const ChannelGrid& grid, float x, float y);

}
