/*
 * SPDX-License-Identifier: MIT
 */

#include "coordinates.h"

#include <cmath>

namespace bitoscope::gui_test_input {

namespace {

float usable(float scale) { return scale > 0.0f ? scale : 1.0f; }

}

LogicalPoint toLogical(PointPx physical, CoordinateScale scale) {
  return LogicalPoint{static_cast<float>(physical.x) / usable(scale.x),
                      static_cast<float>(physical.y) / usable(scale.y)};
}

PointPx toPhysical(float logicalX, float logicalY, CoordinateScale scale) {
  return PointPx{static_cast<std::int32_t>(std::lround(logicalX * usable(scale.x))),
                 static_cast<std::int32_t>(std::lround(logicalY * usable(scale.y)))};
}

}
