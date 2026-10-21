/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <portal_file_chooser.h>

#include <expected>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace bitoscope::gui::linux_platform {

using DialogPump = std::function<bool()>;

inline constexpr int kFileChooserPollMilliseconds = 10;

std::expected<std::optional<std::filesystem::path>, std::string> showFileChooser(
  gui_linux::FileChooserKind kind, const gui_linux::FileChooserTexts& texts, const std::string& parentWindow,
  const DialogPump& pump, std::vector<std::string>& notes);

}
