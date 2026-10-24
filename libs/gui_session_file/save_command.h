/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include <panel_capture.h>

namespace bitoscope::gui_session_file {

enum class SaveCommand {
  Save,
  SaveAs,
};

using KnownFile = gui_capture::PanelCapture::KnownFile;

bool saveAsksWhere(SaveCommand command, const std::optional<KnownFile>& file);

struct SaveDialogStart {
  std::filesystem::path folder;
  std::string fileName;
  bool operator==(const SaveDialogStart&) const = default;
};
SaveDialogStart saveDialogStart(const std::optional<KnownFile>& file, const std::filesystem::path& lastFolder,
                                std::string suggestedName);

enum class SaveWrite {
  Full,
  ViewOnly,
};
enum class KnownFileOnDisk {
  Present,
  Missing,
};
KnownFileOnDisk knownFileOnDisk(const std::optional<KnownFile>& file);

SaveWrite saveWriteFor(const std::optional<KnownFile>& file, const std::filesystem::path& chosen,
                       KnownFileOnDisk onDisk);

bool sameFile(const std::filesystem::path& a, const std::filesystem::path& b);

}
