/*
 * SPDX-License-Identifier: MIT
 */

#include "held_rows.h"

#include <algorithm>

namespace bitoscope::gui_waveform_frame {

void HeldRows::forget(std::uint32_t channels) { held_.assign(channels, 0); }

std::vector<std::uint32_t> HeldRows::missing(std::span<const std::uint32_t> wanted) const {
  std::vector<std::uint32_t> out;
  for (const std::uint32_t row : wanted) {
    if (row < held_.size() && held_[row] == 0) out.push_back(row);
  }
  std::sort(out.begin(), out.end());
  out.erase(std::unique(out.begin(), out.end()), out.end());
  return out;
}

void HeldRows::hold(std::span<const std::uint32_t> rows) {
  for (const std::uint32_t row : rows) {
    if (row < held_.size()) held_[row] = 1;
  }
}

bool HeldRows::holds(std::uint32_t row) const { return row < held_.size() && held_[row] != 0; }

}
