/*
 * SPDX-License-Identifier: MIT
 */

#include "decoder_plugin_set.h"

#include <system_error>

#include "cli_decoder_plugins.h"
#include "plugin_process.h"

namespace bitoscope::gui_decode {

namespace {

std::string pathText(const std::filesystem::path& path) {
  const std::u8string text = path.u8string();
  return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}

std::string joined(const std::vector<std::string>& names) {
  std::string out;
  for (const std::string& name : names) {
    if (!out.empty()) out += ", ";
    out += name;
  }
  return out;
}

}

std::filesystem::path guiDecoderDirectory(const std::filesystem::path& gui_executable) {
  return cli::defaultDecoderDirectory(gui_executable);
}

DecoderDirectory readDecoderDirectory(const std::filesystem::path& directory) {
  std::error_code ignored;
  DecoderDirectory read;
  read.path = directory;
  read.exists = std::filesystem::is_directory(directory, ignored);
  if (read.exists) read.executables = host::listPluginExecutables(directory);
  return read;
}

DecoderPluginSelection selectDecoderPlugins(const DecoderDirectory& directory, std::span<const std::string> names) {
  DecoderPluginSelection selection;
  const std::string where = pathText(directory.path);
  if (!directory.exists) {
    selection.problems.push_back("decoder directory " + where + " does not exist; no decoders are available");
    return selection;
  }
  if (directory.executables.empty()) {
    selection.problems.push_back("no decoder plugins in " + where + "; no decoders are available");
    return selection;
  }
  std::vector<std::string> there;
  there.reserve(directory.executables.size());
  for (const std::filesystem::path& executable : directory.executables) there.push_back(pathText(executable.stem()));
  for (const std::string& name : names) {
    size_t found = there.size();
    for (size_t i = 0; i < there.size(); ++i) {
      if (there[i] == name) found = i;
    }
    if (found == there.size()) {
      selection.problems.push_back("decoder plugin '" + name + "' is not in " + where +
                                   "; its decoders are not available (there: " + joined(there) + ")");
    } else {
      selection.executables.push_back(directory.executables[found]);
    }
  }
  return selection;
}

std::string pluginNotStartedText(std::string_view plugin, std::string_view reason) {
  return "decoder plugin '" + std::string(plugin) + "' did not start: " + std::string(reason) +
         "; its decoders are not available";
}

std::string decoderIgnoredText(std::string_view decoder, std::string_view plugin, std::string_view kept_plugin) {
  return "decoder '" + std::string(decoder) + "' of plugin '" + std::string(plugin) + "' is ignored: plugin '" +
         std::string(kept_plugin) + "' declares it already";
}

}
