/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "test_command.h"

namespace bitoscope::gui_test_input {

struct CoordinateScale {
  float x = 1.0f;
  float y = 1.0f;
};

struct LogicalPoint {
  float x = 0.0f;
  float y = 0.0f;
  friend bool operator==(const LogicalPoint&, const LogicalPoint&) = default;
};

LogicalPoint toLogical(PointPx physical, CoordinateScale scale);

PointPx toPhysical(float logicalX, float logicalY, CoordinateScale scale);

}
