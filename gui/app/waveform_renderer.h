/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <imgui.h>

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace bitoscope::gui {

class WaveformRenderer {
 public:
  struct Lane {
    float top = 0;
    float bottom = 0;
    std::uint32_t row = 0;
    std::array<float, 4> highColor{};
    std::array<float, 4> lowColor{};
    std::array<float, 4> edgeColor{};
    bool needsOutline = false;
    std::array<float, 4> outlineColor{};
    // WHY: Alpha 0 means no fill: the struct stays plain, so no std::optional.
    std::array<float, 4> fillColor{};
  };

  virtual ~WaveformRenderer() = default;

  virtual void upload(std::uint64_t view, std::uint32_t width, std::uint32_t rows,
                      std::span<const std::uint8_t> levels) = 0;

  virtual bool holds(std::uint64_t view, std::uint32_t width, std::uint32_t rows) const = 0;

  virtual void draw(ImDrawList* drawList, std::uint64_t view, float left, std::span<const Lane> lanes,
                    float lineWidth) = 0;

  virtual void release(std::uint64_t view) = 0;

  virtual std::vector<std::string> takeProblems() = 0;
};

}
