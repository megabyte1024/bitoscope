/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace bitoscope::gui_decode {

struct DecoderDirectory {
  std::filesystem::path path;
  bool exists = false;
  std::vector<std::filesystem::path> executables;
};

struct DecoderPluginSelection {
  std::vector<std::filesystem::path> executables;
  std::vector<std::string> problems;
};

std::filesystem::path guiDecoderDirectory(const std::filesystem::path& gui_executable);

DecoderDirectory readDecoderDirectory(const std::filesystem::path& directory);

DecoderPluginSelection selectDecoderPlugins(const DecoderDirectory& directory, std::span<const std::string> names);

std::string pluginNotStartedText(std::string_view plugin, std::string_view reason);

std::string decoderIgnoredText(std::string_view decoder, std::string_view plugin, std::string_view kept_plugin);

}
