/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <array>

namespace bitoscope::waveform_view {

struct DimensionLayout {
  bool labelInside = true;
  double labelX = 0.0;

  bool arrowsOutward = true;
  double leftArrowStart = 0.0;
  double leftArrowEnd = 0.0;
  double rightArrowStart = 0.0;
  double rightArrowEnd = 0.0;
};

DimensionLayout layoutDimension(double x0, double x1, double labelWidth, double arrowLength, double gap,
                                double viewLeft, double viewRight);

struct ArrowTriangle {
  double apexX = 0.0;
  double apexY = 0.0;
  double baseX = 0.0;
  double baseTopY = 0.0;
  double baseBottomY = 0.0;
};

struct Vertex {
  double x = 0.0;
  double y = 0.0;
};

// WHY: Return corners clockwise on screen: Dear ImGui draws the anti-aliased fringe only for clockwise points.
std::array<Vertex, 3> arrowVertices(const ArrowTriangle& arrow);

ArrowTriangle arrowTriangle(double tipX, double y, int direction, int lengthPx, int widthPx);

double dimensionRowY(double y);

int oddPixelWidth(double width);

struct DimensionRows {
  double highY = 0.0;
  double periodY = 0.0;
};

DimensionRows dimensionRows(double waveTop, double waveBottom, double labelFrameHeight, double gap);

enum class PulseEdge { Start, Mid, End };

struct DimensionRowReach {
  bool high = false;
  bool period = false;
};

DimensionRowReach dimensionRowReach(PulseEdge edge);

double dimensionRowsPitch(double waveHeight, double labelFrameHeight, double gap);

}
