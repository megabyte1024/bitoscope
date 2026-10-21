/*
 * SPDX-License-Identifier: MIT
 */

#include "frame_times_probe.h"

#include <utility>

namespace bitoscope::gui_test_input {

FrameTimesProbe::FrameTimesProbe(FrameTimes& times, std::function<std::uint64_t()> clockNs)
    : times_(times), clockNs_(std::move(clockNs)) {}

void FrameTimesProbe::enter(gui_frame_probe::DrawPart part) { times_.enterPart(part, clockNs_()); }

void FrameTimesProbe::leave() { times_.leavePart(clockNs_()); }

}
