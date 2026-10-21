/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>

namespace bitoscope::gui_linux {

enum class ColorScheme {
  NoPreference,
  PreferDark,
  PreferLight,
};

ColorScheme colorSchemeFromPortal(std::uint32_t value);

bool prefersDark(ColorScheme scheme);

}
