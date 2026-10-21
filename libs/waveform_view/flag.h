/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>

namespace bitoscope::waveform_view {

struct FlagRect {
  double minX = 0.0;
  double minY = 0.0;
  double maxX = 0.0;
  double maxY = 0.0;
};

inline bool flagRectContains(const FlagRect& rect, double x, double y) {
  return x >= rect.minX && x <= rect.maxX && y >= rect.minY && y <= rect.maxY;
}

FlagRect flagRect(double x, double top, double textWidth, double textHeight, double em);

struct FlagTextSize {
  double width = 0.0;
  double height = 0.0;
};

FlagRect flagRectFor(double x, double top, bool labelled, FlagTextSize label, FlagTextSize oneChar,
                     double em);

FlagRect widthLabelRect(double midpointX, double rulerLeft, double rulerRight, double rulerTop,
                       double rulerHeight, double textWidth, double textHeight, double em);

struct FlagTextPos {
  double x = 0.0;
  double y = 0.0;
};
FlagTextPos flagTextPosition(const FlagRect& rect, double textWidth, double textHeight);

enum class FlagZone : uint8_t { None, Flag, Line };

FlagZone hitTestFlag(double x, double y, double lineX, const FlagRect& rect, double lineTop,
                     double lineBottom, double grabPixels);

enum class HoverCursor : uint8_t { Default, ResizeEW };

HoverCursor hoverCursorFor(bool overMarkerFlag, bool overCursorFlag, bool overLine, bool overWidthLabel);

}
