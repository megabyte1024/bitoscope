/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <functional>

#include "frame_probe.h"
#include "frame_times.h"

namespace bitoscope::gui_test_input {

class FrameTimesProbe final : public gui_frame_probe::FrameProbe {
 public:
  FrameTimesProbe(FrameTimes& times, std::function<std::uint64_t()> clockNs);

  void enter(gui_frame_probe::DrawPart part) override;
  void leave() override;

 private:
  FrameTimes& times_;
  std::function<std::uint64_t()> clockNs_;
};

}
