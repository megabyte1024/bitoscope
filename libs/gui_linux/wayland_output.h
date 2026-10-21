/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <optional>

namespace bitoscope::gui_linux {

struct LogicalSize {
  int32_t width = 0;
  int32_t height = 0;

  bool operator==(const LogicalSize&) const = default;
};

std::optional<LogicalSize> waylandLogicalOutputSize(int32_t modeWidth, int32_t modeHeight,
                                                    float outputScale,
                                                    std::optional<float> windowScale);

}
