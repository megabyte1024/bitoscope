/*
 * SPDX-License-Identifier: MIT
 */

#include "display_text_file.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlobj.h>
#endif

namespace bitoscope::display_text_file {

namespace {

namespace dt = bitoscope::display_text;

// WHY: _dupenv_s and not getenv: MSVC deprecates getenv at /W4.
std::optional<std::string> environmentValue(const char* name) {
#ifdef _MSC_VER
  char* value = nullptr;
  std::size_t length = 0;
  if (_dupenv_s(&value, &length, name) != 0 || value == nullptr) return std::nullopt;
  std::string out(value);
  std::free(value);
  return out;
#else
  const char* value = std::getenv(name);
  if (value == nullptr) return std::nullopt;
  return std::string(value);
#endif
}

// WHY: Paths come from UTF-8 text: a narrow path string is read in the ANSI code page on Windows.
std::filesystem::path pathFromUtf8(const std::string& utf8) {
  return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
}

// WHY: Uses the POSIX rule on every system so tests do not depend on the system they run on.
bool isAbsolutePosix(const std::optional<std::string>& path) {
  return path && !path->empty() && path->front() == '/';
}

}

AbbreviationFile readAbbreviations(const std::filesystem::path& path) {
  AbbreviationFile out;
  std::error_code ec;
  if (!std::filesystem::exists(path, ec)) return out;
  out.present = true;

  const std::string where = path.filename().string();
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    out.warnings.push_back(where + " cannot be read; built-in abbreviations are used");
    return out;
  }
  std::ostringstream text;
  text << in.rdbuf();

  const nlohmann::json root = nlohmann::json::parse(text.str(), nullptr, false);
  if (root.is_discarded() || !root.is_object()) {
    out.warnings.push_back(where + " is not JSON object; built-in abbreviations are used");
    return out;
  }

  const auto listed = root.find("abbreviations");
  if (listed == root.end()) {
    out.readable = true;
    return out;
  }
  if (!listed->is_array()) {
    out.warnings.push_back(where + ": 'abbreviations' must be list; none read from it");
    return out;
  }
  out.readable = true;

  const auto dropped = [&out](std::string why) {
    out.warnings.push_back(std::move(why));
    ++out.leftOut;
  };
  for (std::size_t i = 0; i < listed->size(); ++i) {
    const nlohmann::json& entry = (*listed)[i];
    const std::string at = where + ": abbreviations[" + std::to_string(i) + "]";
    if (!entry.is_object()) {
      dropped(at + " must be object; left out");
      continue;
    }
    const auto upper = entry.find("upper");
    if (upper == entry.end() || !upper->is_string()) {
      dropped(at + " needs 'upper' as string; left out");
      continue;
    }
    const std::string key = upper->get<std::string>();
    if (const auto problem = dt::problemWith({key, "", false})) {
      dropped(at + ": " + *problem + "; left out");
      continue;
    }
    const auto shown = entry.find("shown");
    if (shown != entry.end() && !shown->is_string()) {
      dropped(at + ": 'shown' must be string; left out");
      continue;
    }
    const auto already = std::find_if(out.abbreviations.begin(), out.abbreviations.end(),
                                      [&key](const dt::Abbreviation& a) { return a.upper == key; });
    if (already != out.abbreviations.end()) {
      dropped(at + ": '" + key + "' is listed twice; later one left out");
      continue;
    }
    const auto alsoWord = entry.find("alsoWord");
    dt::Abbreviation made;
    made.upper = key;
    if (shown != entry.end()) made.shown = shown->get<std::string>();
    made.alsoWord = alsoWord != entry.end() && alsoWord->is_boolean() && alsoWord->get<bool>();
    out.abbreviations.push_back(std::move(made));
  }
  return out;
}

std::filesystem::path settingsDirectory(const SettingsFacts& facts) {
  switch (facts.platform) {
    case Platform::windows:
      if (!facts.roamingAppData || facts.roamingAppData->empty()) return {};
      return pathFromUtf8(*facts.roamingAppData) / "bitoscope";
    case Platform::linuxXdg:
      if (isAbsolutePosix(facts.xdgConfigHome)) return pathFromUtf8(*facts.xdgConfigHome) / "bitoscope";
      if (isAbsolutePosix(facts.home)) return pathFromUtf8(*facts.home) / ".config" / "bitoscope";
      return {};
    case Platform::macos:
      return {};
  }
  return {};
}

SettingsFacts systemSettingsFacts() {
  SettingsFacts facts;
#if defined(_WIN32)
  facts.platform = Platform::windows;
  PWSTR folder = nullptr;
  if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, KF_FLAG_DEFAULT, nullptr, &folder))) {
    const std::u8string utf8 = std::filesystem::path(folder).u8string();
    facts.roamingAppData = std::string(utf8.begin(), utf8.end());
  }
  CoTaskMemFree(folder);  // WHY: CoTaskMemFree is required even when the call failed; null is allowed
#elif defined(__APPLE__)
  facts.platform = Platform::macos;
  facts.home = environmentValue("HOME");
#else
  facts.platform = Platform::linuxXdg;
  facts.xdgConfigHome = environmentValue("XDG_CONFIG_HOME");
  facts.home = environmentValue("HOME");
#endif
  return facts;
}

std::string abbreviationsFromEnvironment() {
  return environmentValue("BITOSCOPE_ABBREVIATIONS").value_or(std::string());
}

std::vector<std::filesystem::path> abbreviationFiles(const AbbreviationSources& sources) {
  std::vector<std::filesystem::path> files;
  if (!sources.besideProgram.empty()) files.push_back(sources.besideProgram / "abbreviations.json");
  if (!sources.settingsDirectory.empty()) files.push_back(sources.settingsDirectory / "abbreviations.json");
  if (!sources.fromEnvironment.empty()) files.push_back(pathFromUtf8(sources.fromEnvironment));
  return files;
}

ApplyResult applyAbbreviations(const AbbreviationSources& sources) {
  ApplyResult result;
  for (const std::filesystem::path& file : abbreviationFiles(sources)) {
    AbbreviationFile read = readAbbreviations(file);
    if (!read.abbreviations.empty()) dt::overrideAbbreviations(read.abbreviations);
    if (read.present && read.readable) {
      std::string line = file.string() + ": " + std::to_string(read.abbreviations.size()) + " added";
      if (read.leftOut > 0) line += ", " + std::to_string(read.leftOut) + " left out";
      result.summaries.push_back(std::move(line));
    }
    for (std::string& warning : read.warnings) result.warnings.push_back(std::move(warning));
  }
  return result;
}

std::vector<std::string> linesToShow(const ApplyResult& result, bool onlyOnProblem) {
  if (onlyOnProblem && result.warnings.empty()) return {};
  std::vector<std::string> lines = result.summaries;
  lines.insert(lines.end(), result.warnings.begin(), result.warnings.end());
  return lines;
}

}
