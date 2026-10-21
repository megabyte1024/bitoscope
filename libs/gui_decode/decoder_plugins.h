/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <chrono>
#include <expected>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "decode_placement.h"
#include "decoder_plugin_pool.h"
#include "decoder_registry.h"
#include "plugin_decoder.h"

namespace bitoscope::gui_decode {

inline constexpr std::chrono::milliseconds kCheckLimit{1000};

struct StartedPlugin {
  std::string name;
  std::expected<std::shared_ptr<decoder_host::DecoderPluginPool>, std::string> pool;
};

struct DecoderPlugins {
  bitoscope::decode::DecoderRegistry registry;
  std::vector<std::shared_ptr<decoder_host::DecoderPluginPool>> pools;
  std::vector<std::string> problems;
};

decoder_host::ClientOptions guiClientOptions(const DecodePlacement& placement);

decoder_host::PoolOptions guiPoolOptions(const DecodePlacement& placement);

DecoderPlugins registryFromPools(std::vector<StartedPlugin> started,
                                 const decoder_host::PluginDecoderOptions& options = {});

DecoderPlugins startDecoderPlugins(const std::filesystem::path& directory, std::span<const std::string> names,
                                   const DecodePlacement& placement,
                                   const decoder_host::PluginDecoderOptions& options = {});

DecoderPlugins startDefaultDecoderPlugins(const std::filesystem::path& gui_executable,
                                          const DecodePlacement& placement,
                                          const decoder_host::PluginDecoderOptions& options = {});

}
