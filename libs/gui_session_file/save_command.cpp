/*
 * SPDX-License-Identifier: MIT
 */

#include "save_command.h"

#include <string_view>
#include <system_error>
#include <utility>

namespace bitoscope::gui_session_file {

namespace {

std::filesystem::path pathOfUtf8(std::string_view text) {
  return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
}

std::string utf8Of(const std::filesystem::path& path) {
  const std::u8string text = path.u8string();
  return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}

std::filesystem::path normalised(const std::filesystem::path& path) {
  std::error_code ec;
  const std::filesystem::path absolute = std::filesystem::absolute(path, ec);
  return (ec ? path : absolute).lexically_normal();
}

}

bool saveAsksWhere(SaveCommand command, const std::optional<KnownFile>& file) {
  return command == SaveCommand::SaveAs || !file.has_value();
}

SaveDialogStart saveDialogStart(const std::optional<KnownFile>& file, const std::filesystem::path& lastFolder,
                                std::string suggestedName) {
  if (file) {
    const std::filesystem::path path = pathOfUtf8(file->path);
    return {path.parent_path(), utf8Of(path.filename())};
  }
  return {lastFolder, std::move(suggestedName)};
}

SaveWrite saveWriteFor(const std::optional<KnownFile>& file, const std::filesystem::path& chosen) {
  return file && sameFile(pathOfUtf8(file->path), chosen) ? SaveWrite::ViewOnly : SaveWrite::Full;
}

bool sameFile(const std::filesystem::path& a, const std::filesystem::path& b) {
  if (normalised(a) == normalised(b)) {
    return true;
  }
  std::error_code ec;
  const bool same = std::filesystem::equivalent(a, b, ec);
  return !ec && same;
}

}
