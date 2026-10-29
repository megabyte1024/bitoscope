/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <span>
#include <string>
#include <string_view>

#include "close_flow.h"
#include "panel_capture.h"

namespace bitoscope::gui_capture {

bool panelUnsavedMark(const PanelCapture& capture);

bool windowUnsavedMark(std::span<const ClosingPanel> panels);

std::string windowTitleText(std::string_view base, bool unsaved);

}
