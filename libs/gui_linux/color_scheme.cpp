/*
 * SPDX-License-Identifier: MIT
 */

#include "color_scheme.h"

namespace bitoscope::gui_linux {

ColorScheme colorSchemeFromPortal(std::uint32_t value) {
  switch (value) {
  case 1:
    return ColorScheme::PreferDark;
  case 2:
    return ColorScheme::PreferLight;
  default:
    return ColorScheme::NoPreference;
  }
}

bool prefersDark(ColorScheme scheme) {
  return scheme == ColorScheme::PreferDark;
}

}
