/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

namespace bitoscope::gui_prefs {

enum class ThemeMode {
  System,
  Light,
  Dark,
};

enum class Theme {
  Light,
  Dark,
};

Theme resolveTheme(ThemeMode mode, bool systemIsDark);

}
