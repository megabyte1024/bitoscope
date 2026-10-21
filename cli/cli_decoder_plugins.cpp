/*
 * SPDX-License-Identifier: MIT
 */

#include "cli_decoder_plugins.h"

#include <algorithm>
#include <span>
#include <utility>

#include "display_text.h"
#include "plugin_process.h"

namespace bitoscope::cli {

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

std::filesystem::path defaultDecoderDirectory(const std::filesystem::path& executable) {
  return executable.parent_path() / "decoders";
}

std::filesystem::path decoderDirectory(std::string_view decoder_dir, const std::filesystem::path& executable) {
  if (!decoder_dir.empty()) return std::filesystem::path(std::u8string(decoder_dir.begin(), decoder_dir.end()));
  return defaultDecoderDirectory(executable);
}

std::expected<std::filesystem::path, std::string> selectDecoderPluginExecutable(
    const std::filesystem::path& directory, std::string_view requested) {
  const std::vector<std::filesystem::path> found = host::listPluginExecutables(directory);
  const std::string where = pathText(directory);
  if (found.empty()) {
    return std::unexpected("no decoder plugins in " + where + " (give decoder directory with --decoder-dir)");
  }
  std::vector<std::string> names;
  names.reserve(found.size());
  for (const std::filesystem::path& path : found) names.push_back(pathText(path.stem()));
  for (size_t i = 0; i < found.size(); ++i) {
    if (names[i] == requested) return found[i];
  }
  return std::unexpected("no decoder plugin \"" + std::string(requested) + "\" in " + where +
                         "; there: " + joined(names));
}

std::vector<std::string> defaultDecoderPlugins() { return {"decoders_lowlevel", "decoders_luajit"}; }

std::expected<std::vector<std::filesystem::path>, std::string> selectDecoderPluginExecutables(
    const std::filesystem::path& directory, std::span<const std::string> names) {
  std::vector<std::filesystem::path> executables;
  executables.reserve(names.size());
  for (const std::string& name : names) {
    auto executable = selectDecoderPluginExecutable(directory, name);
    if (!executable) return std::unexpected(executable.error());
    executables.push_back(std::move(*executable));
  }
  return executables;
}

std::string defaultSetScriptDirRefusal(std::span<const std::string> names) {
  std::string plugins;
  for (const std::string& name : names) plugins += (plugins.empty() ? "" : " and ") + name;
  return "--decoder-script-dir: none of default decoder plugins " + plugins +
         " runs scripts; name plugin that does with --decoder-plugin";
}

std::expected<std::shared_ptr<decoder_host::DecoderPluginClient>, std::string> startDecoderPlugin(
    const std::filesystem::path& executable, std::optional<std::string_view> script_dir,
    decoder_host::ClientOptions options) {
  host::PluginProcessOptions process;
  process.executable = executable;
  process.arguments = {"--device", std::string(decoder_host::kDecoderDeviceId)};
  if (script_dir.has_value()) {
    process.arguments.emplace_back("--script-dir");
    process.arguments.emplace_back(*script_dir);
  }
  if (options.on_log) process.onStderrLine = options.on_log;
  auto started = host::PluginProcess::start(std::move(process));
  if (!started) return std::unexpected(started.error());
  return decoder_host::DecoderPluginClient::connect(std::move(*started), std::move(options));
}

std::expected<void, std::string> scriptDirAccepted(std::string_view plugin_name,
                                                   const ipc::decoder::DecoderFacetInfo& info) {
  if (info.runsScripts) return {};
  return std::unexpected("--decoder-script-dir: decoder plugin '" + std::string(plugin_name) +
                         "' runs no scripts (its decoder facet does not say it does, as decoders_luajit's does)");
}

std::expected<decode::DecoderRegistry, std::string> decoderRegistryFor(
    std::shared_ptr<decoder_host::DecoderPluginClient> client, decoder_host::PluginDecoderOptions options) {
  decode::DecoderRegistry registry;
  if (auto added = decoder_host::addPluginDecoders(registry, std::move(client), std::move(options)); !added) {
    return std::unexpected(added.error());
  }
  return registry;
}

std::expected<std::vector<std::shared_ptr<decoder_host::DecoderPluginClient>>, std::string> startDecoderPlugins(
    std::span<const std::filesystem::path> executables, std::optional<std::string_view> script_dir,
    decoder_host::ClientOptions options) {
  std::vector<std::shared_ptr<decoder_host::DecoderPluginClient>> clients;
  clients.reserve(executables.size());
  for (const std::filesystem::path& executable : executables) {
    auto client = startDecoderPlugin(executable, script_dir, options);
    if (!client) return std::unexpected(client.error());
    clients.push_back(std::move(*client));
  }
  return clients;
}

std::expected<ScriptedStart, std::string> startDecoderPluginsGivingScriptDir(
    std::span<const std::filesystem::path> executables, std::string_view script_dir,
    decoder_host::ClientOptions options) {
  ScriptedStart started;
  started.clients.reserve(executables.size());
  for (const std::filesystem::path& executable : executables) {
    auto client = startDecoderPlugin(executable, std::nullopt, options);
    if (!client) return std::unexpected(client.error());
    if ((*client)->info().runsScripts) {
      client->reset();
      client = startDecoderPlugin(executable, script_dir, options);
      if (!client) return std::unexpected(client.error());
      ++started.took_script_dir;
    }
    started.clients.push_back(std::move(*client));
  }
  return started;
}

std::expected<decode::DecoderRegistry, std::string> decoderRegistryFor(
    std::span<const std::shared_ptr<decoder_host::DecoderPluginClient>> clients,
    decoder_host::PluginDecoderOptions options) {
  decode::DecoderRegistry registry;
  for (const auto& client : clients) {
    if (auto added = decoder_host::addPluginDecoders(registry, client, options); !added) {
      return std::unexpected(added.error());
    }
  }
  return registry;
}

std::vector<std::string> decoderWindowWarnings(std::span<const decode::ChainInstance> decoders) {
  std::vector<std::string> lines;
  for (const decode::ChainInstance& instance : decoders) {
    const auto* plugin_decoder = dynamic_cast<const decoder_host::PluginDecoder*>(instance.decoder.get());
    if (plugin_decoder == nullptr) continue;
    if (const auto warning = plugin_decoder->windowWarningText()) {
      lines.push_back("-P '" + instance.spec + "': " + *warning);
    }
  }
  return lines;
}

std::string describeDecoderPlugin(std::string_view name, const ipc::decoder::DecoderFacetInfo& info) {
  std::string text(name);
  if (!info.pluginVersion.empty()) text += " " + info.pluginVersion;
  text += ": ";
  if (info.decoders.empty()) return text + "no decoders\n";
  text += std::to_string(info.decoders.size()) + (info.decoders.size() == 1 ? " decoder\n" : " decoders\n");
  const auto shown = [](const std::string& name, const std::string& display) {
    const std::string reads = display_text::shownName(name, display);
    return reads == name ? name : reads + " (" + name + ")";
  };
  const auto names = [&shown](const auto& list) {
    std::string joined;
    for (const auto& item : list) joined += (joined.empty() ? "" : ", ") + shown(item.name, item.display);
    return joined;
  };
  for (const ipc::decoder::DecoderInfo& d : info.decoders) {
    text += "  " + shown(d.name, d.display);
    if (!d.version.empty()) text += " " + d.version;
    const std::string channels =
        std::to_string(d.channels.size()) + (d.channels.size() == 1 ? " channel" : " channels");
    if (!d.inputs.empty()) {
      text += std::string(d.inputs.size() == 1 ? ": input " : ": inputs ") + names(d.inputs);
      if (!d.channels.empty()) text += ", " + channels;
    } else {
      text += ": " + channels;
    }
    text += ", " + std::to_string(d.options.size()) + (d.options.size() == 1 ? " option" : " options");
    if (!d.outputs.empty()) text += std::string(d.outputs.size() == 1 ? ", output " : ", outputs ") + names(d.outputs);
    if (!d.protocols.empty()) {
      std::string designed;
      for (const std::string& id : d.protocols) {
        designed += (designed.empty() ? "" : ", ") + display_text::displayName(id);
      }
      text += "; designed for " + designed;
    }
    text += "\n";
  }
  return text;
}

std::vector<std::pair<std::string, ipc::decoder::DecoderFacetInfo>> listDecoderPlugins(
    const std::filesystem::path& directory) {
  std::vector<std::pair<std::string, ipc::decoder::DecoderFacetInfo>> out;
  for (const std::filesystem::path& path : host::listPluginExecutables(directory)) {
    auto client = decoder_host::DecoderPluginClient::start(path);
    if (!client) continue;
    out.emplace_back(pathText(path.stem()), (*client)->info());
  }
  return out;
}

}
