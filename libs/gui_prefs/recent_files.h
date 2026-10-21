/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

// WHY: A file that no longer exists is SHOWN greyed and removed only when chosen: never silently dropped
// (owner decision).

#include <cstddef>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace bitoscope::gui_prefs {

inline constexpr std::size_t kRecentFilesLimit = 10;

bool sameFilePath(std::string_view a, std::string_view b);

std::vector<std::string> withRecentFile(std::vector<std::string> list, std::string_view path);

std::vector<std::string> withoutRecentFile(std::vector<std::string> list, std::string_view path);

std::vector<std::string> normalizedRecentFiles(std::vector<std::string> list);

using FileExists = std::function<bool(std::string_view path)>;

struct RecentFileEntry {
  std::string path;
  std::string name;
  bool exists = true;
  bool operator==(const RecentFileEntry&) const = default;
};

std::vector<RecentFileEntry> recentFileEntries(std::span<const std::string> list, const FileExists& exists);

struct RecentChoice {
  enum class Action { Open, RemovedMissing };
  Action action = Action::Open;
  std::string logLine;
};
RecentChoice chooseRecentFile(std::vector<std::string>& list, std::string_view path, const FileExists& exists);

}
