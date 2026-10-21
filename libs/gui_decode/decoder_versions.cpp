/*
 * SPDX-License-Identifier: MIT
 */

#include "decoder_versions.h"

#include "display_text.h"
#include "plugin_decoder.h"

namespace bitoscope::gui_decode {

DecoderVersions decoderVersionsOf(const bitoscope::decode::DecoderRegistry& registry, std::string_view decoderName) {
  const auto decoder = registry.create(decoderName);
  if (!decoder) return {};
  const auto* plugin = dynamic_cast<const decoder_host::PluginDecoder*>(decoder->get());
  if (plugin == nullptr) return {};
  return DecoderVersions{plugin->pluginName(), plugin->pluginVersion(), plugin->info().version};
}

std::vector<DecoderPluginVersionRow> decoderPluginVersionRows(
    std::span<const std::pair<std::string, bitoscope::ipc::decoder::DecoderFacetInfo>> plugins) {
  std::vector<DecoderPluginVersionRow> rows;
  rows.reserve(plugins.size());
  for (const auto& [name, info] : plugins) {
    DecoderPluginVersionRow row{name, info.pluginVersion, {}};
    for (const bitoscope::ipc::decoder::DecoderInfo& d : info.decoders) {
      row.decoders.push_back({display_text::shownName(d.name, d.display), d.version});
    }
    rows.push_back(std::move(row));
  }
  return rows;
}

std::vector<DecoderPluginVersionRow> decoderPluginVersionRows(
    std::span<const std::shared_ptr<decoder_host::DecoderPluginPool>> pools) {
  std::vector<std::pair<std::string, bitoscope::ipc::decoder::DecoderFacetInfo>> plugins;
  plugins.reserve(pools.size());
  for (const auto& pool : pools) {
    if (pool != nullptr) plugins.emplace_back(pool->name(), pool->info());
  }
  return decoderPluginVersionRows(plugins);
}

std::vector<bitoscope::capture::DecoderVersions> savedDecoderVersionsOf(
    std::span<const DecoderConfig> configs, const bitoscope::decode::DecoderRegistry& registry) {
  std::vector<bitoscope::capture::DecoderVersions> out;
  out.reserve(configs.size());
  for (const DecoderConfig& config : configs) {
    const DecoderVersions versions = decoderVersionsOf(registry, config.decoderName());
    out.push_back({versions.pluginVersion, versions.decoderVersion});
  }
  return out;
}

}
