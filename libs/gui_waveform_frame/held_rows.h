/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace bitoscope::gui_waveform_frame {

class HeldRows {
 public:
  void forget(std::uint32_t channels);
  std::vector<std::uint32_t> missing(std::span<const std::uint32_t> wanted) const;
  void hold(std::span<const std::uint32_t> rows);
  bool holds(std::uint32_t row) const;
  std::uint32_t channels() const { return static_cast<std::uint32_t>(held_.size()); }

 private:
  std::vector<std::uint8_t> held_;
};

}
