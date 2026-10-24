/*
 * SPDX-License-Identifier: MIT
 */

#include "dimension_layout.h"

#include <algorithm>
#include <cmath>

namespace bitoscope::waveform_view {

namespace {

DimensionLayout arrowsPointingIn(double lo, double hi, double arrowLength) {
  DimensionLayout layout;
  layout.labelInside = false;
  layout.arrowsOutward = false;
  layout.leftArrowEnd = lo;
  layout.leftArrowStart = lo - arrowLength;
  layout.rightArrowEnd = hi;
  layout.rightArrowStart = hi + arrowLength;
  return layout;
}

bool inView(double x, double viewLeft, double viewRight) { return x >= viewLeft && x <= viewRight; }

}

DimensionLayout layoutDimension(double x0, double x1, double labelWidth, double arrowLength, double gap,
                                double viewLeft, double viewRight) {
  const double lo = (std::min)(x0, x1);
  const double hi = (std::max)(x0, x1);
  const double span = hi - lo;

  DimensionLayout layout;

  layout.labelInside = span >= labelWidth + 2.0 * gap + 2.0 * arrowLength;

  if (layout.labelInside) {
    layout.arrowsOutward = true;
    layout.leftArrowEnd = lo;
    layout.leftArrowStart = lo + arrowLength;
    layout.rightArrowEnd = hi;
    layout.rightArrowStart = hi - arrowLength;
    const double visibleLo = (std::max)(lo, viewLeft);
    const double visibleHi = (std::min)(hi, viewRight);
    const double centredInView = (visibleLo + visibleHi) * 0.5 - labelWidth * 0.5;
    const double firstBesideArrow = layout.leftArrowStart + gap;
    const double lastBesideArrow = layout.rightArrowStart - gap - labelWidth;
    layout.labelX = (std::max)(firstBesideArrow, (std::min)(centredInView, lastBesideArrow));
    if (inView(layout.labelX, viewLeft, viewRight) && inView(layout.labelX + labelWidth, viewLeft, viewRight)) {
      return layout;
    }

    DimensionLayout outside = arrowsPointingIn(lo, hi, arrowLength);
    const double rightOfLine = outside.rightArrowStart + gap;
    const double leftOfLine = outside.leftArrowStart - gap - labelWidth;
    if (inView(hi, viewLeft, viewRight) && rightOfLine + labelWidth <= viewRight) {
      outside.labelX = rightOfLine;
      return outside;
    }
    if (inView(lo, viewLeft, viewRight) && leftOfLine >= viewLeft) {
      outside.labelX = leftOfLine;
      return outside;
    }
    return layout;
  }

  layout = arrowsPointingIn(lo, hi, arrowLength);

  const double rightCandidate = layout.rightArrowStart + gap;
  if (rightCandidate + labelWidth <= viewRight) {
    layout.labelX = rightCandidate;
  } else {
    layout.labelX = layout.leftArrowStart - gap - labelWidth;
  }
  return layout;
}

ArrowTriangle arrowTriangle(double tipX, double y, int direction, int lengthPx, int widthPx) {
  // WHY: Do not snap tipX: it is already the GPU pass's per-column anchor, overlayColumnX().
  const double apexX = tipX;
  const double apexY = dimensionRowY(y) + 0.5;
  const double dir = direction < 0 ? -1.0 : 1.0;
  const double halfWidth = static_cast<double>(widthPx / 2);

  ArrowTriangle tri;
  tri.apexX = apexX;
  tri.apexY = apexY;
  tri.baseX = apexX + dir * static_cast<double>(lengthPx);
  tri.baseTopY = apexY - halfWidth;
  tri.baseBottomY = apexY + halfWidth;
  return tri;
}

std::array<Vertex, 3> arrowVertices(const ArrowTriangle& arrow) {
  const Vertex apex{arrow.apexX, arrow.apexY};
  const Vertex top{arrow.baseX, arrow.baseTopY};
  const Vertex bottom{arrow.baseX, arrow.baseBottomY};
  if (arrow.baseX >= arrow.apexX) return {apex, top, bottom};
  return {apex, bottom, top};
}

DimensionRows dimensionRows(double waveTop, double waveBottom, double labelFrameHeight, double gap) {
  DimensionRows rows;
  rows.highY = waveTop - gap - labelFrameHeight * 0.5;
  rows.periodY = waveBottom + gap + labelFrameHeight * 0.5;
  return rows;
}

double dimensionRowsPitch(double waveHeight, double labelFrameHeight, double gap) {
  return waveHeight + 2.0 * (gap + labelFrameHeight);
}

double dimensionRowY(double y) { return std::round(y); }

DimensionRowReach dimensionRowReach(PulseEdge edge) {
  switch (edge) {
    case PulseEdge::Mid:
      return {true, false};
    case PulseEdge::Start:
      return {true, true};
    case PulseEdge::End:
      return {false, true};
  }
  return {};
}

int oddPixelWidth(double width) {
  int w = (std::max)(1, static_cast<int>(std::lround(width)));
  if (w % 2 == 0) {
    ++w;
  }
  return w;
}

}
