/*
 * SPDX-License-Identifier: MIT
 */

#include "color_scheme_state.h"

namespace bitoscope::gui_linux {

bool applyColorSchemeAnswer(ColorSchemeState& state, ColorScheme answer, bool first) {
  const bool changed = (answer != state.scheme) && (!first || state.firstTimedOut);
  state.scheme = answer;
  if (first) {
    state.firstAnswered = true;
  }
  return changed;
}

}
