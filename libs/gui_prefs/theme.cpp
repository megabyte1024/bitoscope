/*
 * SPDX-License-Identifier: MIT
 */

#include "theme.h"

namespace bitoscope::gui_prefs {

Theme resolveTheme(ThemeMode mode, bool systemIsDark) {
  switch (mode) {
    case ThemeMode::Light:
      return Theme::Light;
    case ThemeMode::Dark:
      return Theme::Dark;
    case ThemeMode::System:
      return systemIsDark ? Theme::Dark : Theme::Light;
  }
  // WHY: Unreachable for a valid ThemeMode; keeps MSVC /W4 quiet about a missing return.
  return Theme::Light;
}

}
