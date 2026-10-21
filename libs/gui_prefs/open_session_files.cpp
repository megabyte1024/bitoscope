/*
 * SPDX-License-Identifier: MIT
 */

#include "open_session_files.h"

#include <algorithm>
#include <utility>

#include "recent_files.h"

namespace bitoscope::gui_prefs {

OpenSessionFiles normalizedOpenSessionFiles(OpenSessionFiles list) {
  OpenSessionFiles out;
  for (std::size_t i = 0; i < list.files.size(); ++i) {
    std::string& entry = list.files[i];
    if (entry.empty()) {
      continue;
    }
    const auto same = std::find_if(out.files.begin(), out.files.end(),
                                   [&](const std::string& kept) { return sameFilePath(kept, entry); });
    const std::size_t place = static_cast<std::size_t>(same - out.files.begin());
    if (same == out.files.end()) {
      if (out.files.size() == kOpenSessionFilesLimit) {
        continue;
      }
      out.files.push_back(std::move(entry));
    }
    if (list.selected == i) {
      out.selected = place;
    }
  }
  return out;
}

OpenSessionFiles openSessionFilesFor(std::span<const std::string> tabFiles,
                                     std::span<const std::string> pendingFiles, std::string_view selectedFile) {
  OpenSessionFiles raw;
  raw.files.assign(tabFiles.begin(), tabFiles.end());
  raw.files.insert(raw.files.end(), pendingFiles.begin(), pendingFiles.end());
  if (!selectedFile.empty()) {
    for (std::size_t i = 0; i < raw.files.size(); ++i) {
      if (!raw.files[i].empty() && sameFilePath(raw.files[i], selectedFile)) {
        raw.selected = i;
        break;
      }
    }
  }
  return normalizedOpenSessionFiles(std::move(raw));
}

std::optional<std::string> selectedOpenSessionFile(const OpenSessionFiles& list) {
  if (!list.selected || *list.selected >= list.files.size()) {
    return std::nullopt;
  }
  return list.files[*list.selected];
}

}
