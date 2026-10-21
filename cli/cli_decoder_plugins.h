/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "decoder_chain.h"
#include "decoder_plugin_client.h"
#include "decoder_registry.h"
#include "ipc_decoder.h"
#include "plugin_decoder.h"

namespace bitoscope::cli {

std::filesystem::path defaultDecoderDirectory(const std::filesystem::path& executable);

std::filesystem::path decoderDirectory(std::string_view decoder_dir, const std::filesystem::path& executable);

std::expected<std::filesystem::path, std::string> selectDecoderPluginExecutable(
    const std::filesystem::path& directory, std::string_view requested);

std::vector<std::string> defaultDecoderPlugins();

std::expected<std::vector<std::filesystem::path>, std::string> selectDecoderPluginExecutables(
    const std::filesystem::path& directory, std::span<const std::string> names);

std::string defaultSetScriptDirRefusal(std::span<const std::string> names);

std::expected<std::shared_ptr<decoder_host::DecoderPluginClient>, std::string> startDecoderPlugin(
    const std::filesystem::path& executable, std::optional<std::string_view> script_dir,
    decoder_host::ClientOptions options = {});

std::expected<void, std::string> scriptDirAccepted(std::string_view plugin_name,
                                                   const ipc::decoder::DecoderFacetInfo& info);

std::expected<std::vector<std::shared_ptr<decoder_host::DecoderPluginClient>>, std::string> startDecoderPlugins(
    std::span<const std::filesystem::path> executables, std::optional<std::string_view> script_dir,
    decoder_host::ClientOptions options = {});

struct ScriptedStart {
  std::vector<std::shared_ptr<decoder_host::DecoderPluginClient>> clients;
  size_t took_script_dir = 0;
};

std::expected<ScriptedStart, std::string> startDecoderPluginsGivingScriptDir(
    std::span<const std::filesystem::path> executables, std::string_view script_dir,
    decoder_host::ClientOptions options = {});

std::expected<decode::DecoderRegistry, std::string> decoderRegistryFor(
    std::shared_ptr<decoder_host::DecoderPluginClient> client, decoder_host::PluginDecoderOptions options = {});

std::expected<decode::DecoderRegistry, std::string> decoderRegistryFor(
    std::span<const std::shared_ptr<decoder_host::DecoderPluginClient>> clients,
    decoder_host::PluginDecoderOptions options = {});

std::vector<std::string> decoderWindowWarnings(std::span<const decode::ChainInstance> decoders);

std::string describeDecoderPlugin(std::string_view name, const ipc::decoder::DecoderFacetInfo& info);

std::vector<std::pair<std::string, ipc::decoder::DecoderFacetInfo>> listDecoderPlugins(
    const std::filesystem::path& directory);

}
