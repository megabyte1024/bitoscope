/*
 * SPDX-License-Identifier: MIT
 */

#include "preferences_file.h"

#include "preferences_json.h"

#include <format>
#include <fstream>
#include <sstream>
#include <system_error>

namespace bitoscope::gui_prefs {

std::expected<Preferences, std::string> loadPreferences(const std::filesystem::path& path,
                                                        std::vector<std::string>* warnings,
                                                        LegacyPlacementTarget legacyPlacementTarget) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return std::unexpected(std::format("{}: could not be opened", path.string()));
  }
  std::ostringstream buffer;
  buffer << in.rdbuf();
  if (!in.good() && !in.eof()) {
    return std::unexpected(std::format("{}: could not be read", path.string()));
  }
  return parsePreferences(buffer.str(), warnings, legacyPlacementTarget);
}

StartupPreferences loadPreferencesOrDefaults(const std::filesystem::path& path,
                                             LegacyPlacementTarget legacyPlacementTarget) {
  StartupPreferences result;
  std::error_code ec;
  result.fileFound = std::filesystem::exists(path, ec);
  if (!result.fileFound) {
    // WHY: Very first start opens an empty window with no session panel (owner decision).
    result.preferences.sessionPanels.clear();
    return result;
  }
  auto loaded = loadPreferences(path, &result.warnings, legacyPlacementTarget);
  if (loaded) {
    result.preferences = std::move(*loaded);
  } else {
    result.warnings.clear();
    result.problem = loaded.error().starts_with(path.string())
                       ? loaded.error()
                       : std::format("{}: {}", path.string(), loaded.error());
  }
  return result;
}

std::expected<void, std::string> savePreferences(const std::filesystem::path& path,
                                                  const Preferences& preferences) {
  const std::filesystem::path tempPath = path.string() + ".tmp";

  if (path.has_parent_path()) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
      return std::unexpected(std::format("{}: could not be created: {}",
                                         path.parent_path().string(), ec.message()));
    }
  }

  {
    std::ofstream out(tempPath, std::ios::binary | std::ios::trunc);
    if (!out) {
      return std::unexpected(std::format("{}: could not be created", tempPath.string()));
    }
    const std::string json = serializePreferences(preferences);
    out.write(json.data(), static_cast<std::streamsize>(json.size()));
    if (!out) {
      return std::unexpected(std::format("{}: could not be written", tempPath.string()));
    }
  }

  std::error_code ec;
  std::filesystem::rename(tempPath, path, ec);
  if (ec) {
    return std::unexpected(
        std::format("{}: could not be renamed to {}: {}", tempPath.string(), path.string(), ec.message()));
  }

  return {};
}

}
