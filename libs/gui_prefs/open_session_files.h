/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace bitoscope::gui_prefs {

inline constexpr std::size_t kOpenSessionFilesLimit = 32;

struct OpenSessionFiles {
  std::vector<std::string> files;
  std::optional<std::size_t> selected;

  bool operator==(const OpenSessionFiles&) const = default;
};

OpenSessionFiles normalizedOpenSessionFiles(OpenSessionFiles list);

OpenSessionFiles openSessionFilesFor(std::span<const std::string> tabFiles,
                                     std::span<const std::string> pendingFiles, std::string_view selectedFile);

std::optional<std::string> selectedOpenSessionFile(const OpenSessionFiles& list);

}
