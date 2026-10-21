/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <expected>
#include <string>
#include <string_view>
#include <vector>

#include "preferences.h"

namespace bitoscope::gui_prefs {

enum class LegacyPlacementTarget {
  Drop,
  MapToWin32,
};

std::expected<Preferences, std::string> parsePreferences(
  std::string_view json, std::vector<std::string>* warnings = nullptr,
  LegacyPlacementTarget legacyPlacementTarget = LegacyPlacementTarget::Drop);

std::string serializePreferences(const Preferences& preferences);

}
