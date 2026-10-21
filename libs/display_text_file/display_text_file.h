/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "display_text.h"

namespace bitoscope::display_text_file {

struct AbbreviationFile {
  std::vector<display_text::Abbreviation> abbreviations;
  std::vector<std::string> warnings;
  bool present = false;
  bool readable = false;
  std::size_t leftOut = 0;
};

AbbreviationFile readAbbreviations(const std::filesystem::path& path);

enum class Platform { windows, linuxXdg, macos };

struct SettingsFacts {
  Platform platform = Platform::windows;
  std::optional<std::string> roamingAppData;
  std::optional<std::string> xdgConfigHome;
  std::optional<std::string> home;
};

std::filesystem::path settingsDirectory(const SettingsFacts& facts);

SettingsFacts systemSettingsFacts();

struct AbbreviationSources {
  std::filesystem::path besideProgram;
  std::filesystem::path settingsDirectory;
  std::string fromEnvironment;
};

std::string abbreviationsFromEnvironment();

std::vector<std::filesystem::path> abbreviationFiles(const AbbreviationSources& sources);

struct ApplyResult {
  std::vector<std::string> summaries;
  std::vector<std::string> warnings;
};

ApplyResult applyAbbreviations(const AbbreviationSources& sources);

std::vector<std::string> linesToShow(const ApplyResult& result, bool onlyOnProblem);

}
