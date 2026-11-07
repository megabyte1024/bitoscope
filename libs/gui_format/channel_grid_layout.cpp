/*
 * SPDX-License-Identifier: MIT
 */

#include "channel_grid_layout.h"

#include <algorithm>

namespace bitoscope::gui_format {

ChannelGrid channelGrid(std::size_t channels, const ChannelGridSpec& spec) {
  ChannelGrid grid;
  grid.tileWidth = spec.tileWidth;
  grid.tileHeight = spec.tileHeight;
  if (channels == 0) {
    return grid;
  }
  const std::size_t perRow = (std::max)(std::size_t{1}, spec.perRow);
  grid.cells.reserve(channels);
  for (std::size_t i = 0; i < channels; ++i) {
    const std::size_t row = i / perRow;
    const std::size_t column = i % perRow;
    const std::size_t groupsBefore = spec.groupLength == 0 ? 0 : column / spec.groupLength;
    const float x = static_cast<float>(column) * (spec.tileWidth + spec.gapX) +
                    static_cast<float>(groupsBefore) * (spec.groupGap - spec.gapX);
    const float y = static_cast<float>(row) * (spec.tileHeight + spec.gapY);
    grid.cells.push_back(ChannelGridCell{.x = x, .y = y});
    grid.width = (std::max)(grid.width, x + spec.tileWidth);
    grid.height = (std::max)(grid.height, y + spec.tileHeight);
  }
  return grid;
}

std::optional<std::size_t> channelGridCellAt(const ChannelGrid& grid, float x, float y) {
  for (std::size_t i = 0; i < grid.cells.size(); ++i) {
    const ChannelGridCell& cell = grid.cells[i];
    if (x >= cell.x && x < cell.x + grid.tileWidth && y >= cell.y && y < cell.y + grid.tileHeight) {
      return i;
    }
  }
  return std::nullopt;
}

}
