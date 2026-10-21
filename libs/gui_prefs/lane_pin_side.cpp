/*
 * SPDX-License-Identifier: MIT
 */

#include "lane_pin_side.h"

namespace bitoscope::gui_prefs {

waveform_view::PinSide effectiveLanePinSide(std::optional<waveform_view::PinSide> tabSide,
                                            waveform_view::PinSide guiWideSide) {
  return tabSide.value_or(guiWideSide);
}

void recordLanePinned(waveform_view::PinSide side, std::optional<waveform_view::PinSide>& tabSide,
                      waveform_view::PinSide& guiWideSide) {
  tabSide = side;
  guiWideSide = side;
}

}
