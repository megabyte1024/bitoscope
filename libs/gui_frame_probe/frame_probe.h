/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace bitoscope::gui_frame_probe {

enum class DrawPart : std::uint8_t {
  Prologue,
  Columns,
  Upload,
  Headers,
  DecoderLanes,
  DotsOverlays,
  Popups,
};

inline constexpr std::size_t kDrawPartCount = 7;

std::string_view drawPartName(DrawPart part);

class FrameProbe {
 public:
  virtual ~FrameProbe();
  virtual void enter(DrawPart part) = 0;
  virtual void leave() = 0;
};

class DrawPartScope {
 public:
  DrawPartScope(FrameProbe* probe, DrawPart part);
  ~DrawPartScope();
  DrawPartScope(const DrawPartScope&) = delete;
  DrawPartScope& operator=(const DrawPartScope&) = delete;

  void next(DrawPart part);

 private:
  FrameProbe* probe_;
};

}

#ifdef BITOSCOPE_GUI_TEST_HOOK
#define BITOSCOPE_DRAW_PART(name, probe, part) \
  ::bitoscope::gui_frame_probe::DrawPartScope name((probe), ::bitoscope::gui_frame_probe::DrawPart::part)
#define BITOSCOPE_DRAW_PART_NEXT(name, part) (name).next(::bitoscope::gui_frame_probe::DrawPart::part)
#else
#define BITOSCOPE_DRAW_PART(name, probe, part) static_cast<void>(0)
#define BITOSCOPE_DRAW_PART_NEXT(name, part) static_cast<void>(0)
#endif
