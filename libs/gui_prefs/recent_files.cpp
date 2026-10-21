/*
 * SPDX-License-Identifier: MIT
 */

#include "recent_files.h"

#include <algorithm>
#include <filesystem>
#include <format>
#include <utility>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace bitoscope::gui_prefs {

namespace {

std::filesystem::path comparable(std::string_view utf8) {
  const std::u8string text(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size());
  std::filesystem::path path(text);
  path = path.lexically_normal();
  path.make_preferred();
  return path;
}

}

bool sameFilePath(std::string_view a, std::string_view b) {
  const std::filesystem::path pa = comparable(a);
  const std::filesystem::path pb = comparable(b);
#ifdef _WIN32
  const std::wstring& wa = pa.native();
  const std::wstring& wb = pb.native();
  return CompareStringOrdinal(wa.c_str(), static_cast<int>(wa.size()), wb.c_str(), static_cast<int>(wb.size()),
                              TRUE) == CSTR_EQUAL;
#else
  // WHY: Compare exactly on macOS (unverified there): the safe side shows a repeat twice rather than losing a file.
  return pa.native() == pb.native();
#endif
}

std::vector<std::string> withRecentFile(std::vector<std::string> list, std::string_view path) {
  if (path.empty()) {
    return list;
  }
  list = withoutRecentFile(std::move(list), path);
  list.insert(list.begin(), std::string(path));
  if (list.size() > kRecentFilesLimit) {
    list.resize(kRecentFilesLimit);
  }
  return list;
}

std::vector<std::string> withoutRecentFile(std::vector<std::string> list, std::string_view path) {
  std::erase_if(list, [&](const std::string& entry) { return sameFilePath(entry, path); });
  return list;
}

std::vector<std::string> normalizedRecentFiles(std::vector<std::string> list) {
  std::vector<std::string> out;
  for (std::string& entry : list) {
    if (entry.empty()) {
      continue;
    }
    const bool repeat =
        std::any_of(out.begin(), out.end(), [&](const std::string& kept) { return sameFilePath(kept, entry); });
    if (!repeat) {
      out.push_back(std::move(entry));
    }
    if (out.size() == kRecentFilesLimit) {
      break;
    }
  }
  return out;
}

std::vector<RecentFileEntry> recentFileEntries(std::span<const std::string> list, const FileExists& exists) {
  std::vector<RecentFileEntry> out;
  out.reserve(list.size());
  for (const std::string& path : list) {
    const std::filesystem::path p(std::u8string(reinterpret_cast<const char8_t*>(path.data()), path.size()));
    const std::u8string name = p.filename().u8string();
    out.push_back(RecentFileEntry{path, std::string(reinterpret_cast<const char*>(name.data()), name.size()),
                                  exists ? exists(path) : true});
  }
  return out;
}

RecentChoice chooseRecentFile(std::vector<std::string>& list, std::string_view path, const FileExists& exists) {
  if (!exists || exists(path)) {
    return RecentChoice{RecentChoice::Action::Open, {}};
  }
  list = withoutRecentFile(std::move(list), path);
  return RecentChoice{RecentChoice::Action::RemovedMissing,
                      std::format("{} no longer exists; removed from recent files", path)};
}

}
