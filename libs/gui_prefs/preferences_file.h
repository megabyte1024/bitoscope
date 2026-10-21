/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "preferences.h"
#include "preferences_json.h"

namespace bitoscope::gui_prefs {

std::expected<Preferences, std::string> loadPreferences(
  const std::filesystem::path& path, std::vector<std::string>* warnings = nullptr,
  LegacyPlacementTarget legacyPlacementTarget = LegacyPlacementTarget::Drop);

struct StartupPreferences {
  Preferences preferences;
  bool fileFound = false;
  std::optional<std::string> problem;
  std::vector<std::string> warnings;
};
StartupPreferences loadPreferencesOrDefaults(
  const std::filesystem::path& path,
  LegacyPlacementTarget legacyPlacementTarget = LegacyPlacementTarget::Drop);

// WHY: Write a temporary file and rename it over the old one: a crash mid-write must not leave a half-written file.
std::expected<void, std::string> savePreferences(const std::filesystem::path& path,
                                                  const Preferences& preferences);

}
