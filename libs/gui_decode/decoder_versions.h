/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "capture_writer.h"
#include "decoder_config.h"
#include "decoder_plugin_pool.h"
#include "decoder_registry.h"
#include "ipc_decoder.h"

namespace bitoscope::gui_decode {

struct DecoderVersions {
  std::string plugin;
  std::string pluginVersion;
  std::string decoderVersion;
  bool operator==(const DecoderVersions&) const = default;
};

struct DecoderVersionRow {
  std::string decoder;
  std::string version;
  bool operator==(const DecoderVersionRow&) const = default;
};

struct DecoderPluginVersionRow {
  std::string plugin;
  std::string version;
  std::vector<DecoderVersionRow> decoders;
  bool operator==(const DecoderPluginVersionRow&) const = default;
};

DecoderVersions decoderVersionsOf(const bitoscope::decode::DecoderRegistry& registry, std::string_view decoderName);

std::vector<DecoderPluginVersionRow> decoderPluginVersionRows(
    std::span<const std::pair<std::string, bitoscope::ipc::decoder::DecoderFacetInfo>> plugins);

std::vector<DecoderPluginVersionRow> decoderPluginVersionRows(
    std::span<const std::shared_ptr<decoder_host::DecoderPluginPool>> pools);

std::vector<bitoscope::capture::DecoderVersions> savedDecoderVersionsOf(
    std::span<const DecoderConfig> configs, const bitoscope::decode::DecoderRegistry& registry);

}
