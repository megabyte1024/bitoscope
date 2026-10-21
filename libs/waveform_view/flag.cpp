/*
 * SPDX-License-Identifier: MIT
 */

#include "flag.h"

#include <algorithm>
#include <cmath>

namespace bitoscope::waveform_view {

FlagRect flagRect(double x, double top, double textWidth, double textHeight, double em) {
  const double padX = em * 0.25;
  const double padY = em * 0.1;
  const double left = x - textWidth * 0.5 - padX;
  const double right = x + textWidth * 0.5 + padX;
  return FlagRect{left, top, right, top + textHeight + padY * 2.0};
}

FlagRect flagRectFor(double x, double top, bool labelled, FlagTextSize label, FlagTextSize oneChar,
                     double em) {
  const FlagTextSize size = labelled ? label : oneChar;
  return flagRect(x, top, size.width, size.height, em);
}

FlagRect widthLabelRect(double midpointX, double rulerLeft, double rulerRight, double rulerTop,
                       double rulerHeight, double textWidth, double textHeight, double em) {
  const double padX = em * 0.3;
  const double padY = em * 0.1;
  const double centreX =
    (std::clamp)(midpointX, rulerLeft + textWidth * 0.5 + padX, rulerRight - textWidth * 0.5 - padX);
  const double atX = centreX - textWidth * 0.5;
  const double atY = rulerTop + rulerHeight + em * 0.15 + padY;
  return FlagRect{atX - padX, atY - padY, atX + textWidth + padX, atY + textHeight + padY};
}

FlagTextPos flagTextPosition(const FlagRect& rect, double textWidth, double textHeight) {
  const double x = rect.minX + ((rect.maxX - rect.minX) - textWidth) * 0.5;
  const double y = rect.minY + ((rect.maxY - rect.minY) - textHeight) * 0.5;
  // WHY: Round to whole units: ImGui truncates text x (IM_TRUNC), which would put the glyph left of centre.
  return FlagTextPos{std::floor(x + 0.5), std::floor(y + 0.5)};
}

FlagZone hitTestFlag(double x, double y, double lineX, const FlagRect& rect, double lineTop,
                     double lineBottom, double grabPixels) {
  if (flagRectContains(rect, x, y)) {
    return FlagZone::Flag;
  }
  if (y >= lineTop && y <= lineBottom && std::abs(x - lineX) <= grabPixels) {
    return FlagZone::Line;
  }
  return FlagZone::None;
}

HoverCursor hoverCursorFor(bool overMarkerFlag, bool overCursorFlag, bool overLine, bool overWidthLabel) {
  if (overMarkerFlag) {
    return HoverCursor::Default;
  }
  if (overCursorFlag || overLine || overWidthLabel) {
    return HoverCursor::ResizeEW;
  }
  return HoverCursor::Default;
}

}
