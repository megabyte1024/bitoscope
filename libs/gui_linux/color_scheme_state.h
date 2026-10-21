/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "color_scheme.h"

namespace bitoscope::gui_linux {

struct ColorSchemeState {
  ColorScheme scheme = ColorScheme::NoPreference;
  bool firstAnswered = false;
  bool firstTimedOut = false;
};

bool applyColorSchemeAnswer(ColorSchemeState& state, ColorScheme answer, bool first);

}
