/*
 * SPDX-License-Identifier: MIT
 */

#include "decoder_plugins.h"

#include <map>
#include <utility>

#include "cli_decoder_plugins.h"
#include "decoder_plugin_set.h"

namespace bitoscope::gui_decode {

namespace {

std::string stemText(const std::filesystem::path& executable) {
  const std::u8string text = executable.stem().u8string();
  return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}

}

decoder_host::ClientOptions guiClientOptions(const DecodePlacement& placement) {
  decoder_host::ClientOptions options;
  options.placement = placement.work;
  options.check_timeout = kCheckLimit;
  return options;
}

decoder_host::PoolOptions guiPoolOptions(const DecodePlacement& placement) {
  decoder_host::PoolOptions options;
  options.client = guiClientOptions(placement);
  return options;
}

DecoderPlugins registryFromPools(std::vector<StartedPlugin> started, const decoder_host::PluginDecoderOptions& options) {
  DecoderPlugins plugins;
  std::map<std::string, std::string> served_by;
  for (StartedPlugin& plugin : started) {
    if (!plugin.pool) {
      plugins.problems.push_back(pluginNotStartedText(plugin.name, plugin.pool.error()));
      continue;
    }
    const std::shared_ptr<decoder_host::DecoderPluginPool> pool = *plugin.pool;
    bool serves = false;
    for (const ipc::decoder::DecoderInfo& info : pool->info().decoders) {
      if (const auto taken = served_by.find(info.name); taken != served_by.end()) {
        plugins.problems.push_back(decoderIgnoredText(info.name, plugin.name, taken->second));
        continue;
      }
      auto added = plugins.registry.add(info.name, [pool, info, options] {
        return std::make_unique<decoder_host::PluginDecoder>(pool, info, options);
      });
      if (!added) {
        plugins.problems.push_back(std::move(added.error()));
        continue;
      }
      served_by.emplace(info.name, plugin.name);
      serves = true;
    }
    if (serves) plugins.pools.push_back(pool);
  }
  return plugins;
}

DecoderPlugins startDecoderPlugins(const std::filesystem::path& directory, std::span<const std::string> names,
                                   const DecodePlacement& placement, const decoder_host::PluginDecoderOptions& options) {
  const DecoderPluginSelection selection = selectDecoderPlugins(readDecoderDirectory(directory), names);
  std::vector<StartedPlugin> started;
  started.reserve(selection.executables.size());
  for (const std::filesystem::path& executable : selection.executables) {
    started.push_back({stemText(executable), decoder_host::DecoderPluginPool::start(executable, guiPoolOptions(placement))});
  }
  DecoderPlugins plugins = registryFromPools(std::move(started), options);
  plugins.problems.insert(plugins.problems.begin(), selection.problems.begin(), selection.problems.end());
  return plugins;
}

DecoderPlugins startDefaultDecoderPlugins(const std::filesystem::path& gui_executable, const DecodePlacement& placement,
                                          const decoder_host::PluginDecoderOptions& options) {
  const std::vector<std::string> names = cli::defaultDecoderPlugins();
  return startDecoderPlugins(guiDecoderDirectory(gui_executable), names, placement, options);
}

}
