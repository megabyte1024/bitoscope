/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <optional>

#include <lane_order.h>

namespace bitoscope::gui_prefs {

waveform_view::PinSide effectiveLanePinSide(std::optional<waveform_view::PinSide> tabSide,
                                            waveform_view::PinSide guiWideSide);

void recordLanePinned(waveform_view::PinSide side, std::optional<waveform_view::PinSide>& tabSide,
                      waveform_view::PinSide& guiWideSide);

}
