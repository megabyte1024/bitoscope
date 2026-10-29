/*
 * SPDX-License-Identifier: MIT
 */

#include "unsaved_mark.h"

#include <algorithm>

namespace bitoscope::gui_capture {

bool panelUnsavedMark(const PanelCapture& capture) { return capture.hasUnsavedCapture(); }

bool windowUnsavedMark(std::span<const ClosingPanel> panels) {
  return std::ranges::any_of(panels, [](const ClosingPanel& panel) { return panel.unsaved; });
}

std::string windowTitleText(std::string_view base, bool unsaved) {
  return unsaved ? "* " + std::string(base) : std::string(base);
}

}
