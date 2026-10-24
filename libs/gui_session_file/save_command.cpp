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

KnownFileOnDisk knownFileOnDisk(const std::optional<KnownFile>& file) {
  if (!file) {
    return KnownFileOnDisk::Missing;
  }
  std::error_code ec;
  const bool present = std::filesystem::is_regular_file(pathOfUtf8(file->path), ec);
  return present && !ec ? KnownFileOnDisk::Present : KnownFileOnDisk::Missing;
}

SaveWrite saveWriteFor(const std::optional<KnownFile>& file, const std::filesystem::path& chosen,
                       KnownFileOnDisk onDisk) {
  const bool ownFile = file && sameFile(pathOfUtf8(file->path), chosen);
  return ownFile && onDisk == KnownFileOnDisk::Present ? SaveWrite::ViewOnly : SaveWrite::Full;
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
