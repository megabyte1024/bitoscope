/*
 * SPDX-License-Identifier: MIT
 */

#include "decoder_save.h"

#include <utility>

namespace bitoscope::gui_decode {

namespace {

std::optional<std::string> specTextNow(std::span<const DecoderConfig> configs, std::size_t i,
                                       const decode::DecoderRegistry& registry) {
  auto decoder = registry.create(configs[i].decoderName());
  if (!decoder) return std::nullopt;
  return configs[i].toSpecString(**decoder, configs);
}

}

void recordLoadedSetup(std::span<DecoderConfig> configs, std::span<const std::optional<std::size_t>> entries,
                       const decode::DecoderRegistry& registry) {
  std::vector<std::optional<DecoderConfig::LoadedFrom>> baselines(configs.size());
  for (std::size_t i = 0; i < configs.size() && i < entries.size(); ++i) {
    if (!entries[i]) continue;
    if (auto text = specTextNow(configs, i, registry)) {
      baselines[i] = DecoderConfig::LoadedFrom{*entries[i], std::move(*text)};
    }
  }
  for (std::size_t i = 0; i < configs.size(); ++i) configs[i].setLoadedFrom(std::move(baselines[i]));
}

std::vector<std::optional<std::size_t>> unchangedFileEntries(std::span<const DecoderConfig> configs,
                                                             const decode::DecoderRegistry& registry) {
  std::vector<std::optional<std::size_t>> out(configs.size());
  for (std::size_t i = 0; i < configs.size(); ++i) {
    const auto& from = configs[i].loadedFrom();
    if (!from) continue;
    const auto text = specTextNow(configs, i, registry);
    if (text && *text == from->specText) out[i] = from->entry;
  }
  return out;
}

std::vector<std::string> decoderLaneKeys(std::span<const DecoderConfig> configs) {
  std::vector<std::string> keys;
  keys.reserve(configs.size());
  for (const DecoderConfig& config : configs) keys.push_back(config.id());
  return keys;
}

std::vector<waveform_view::DecoderVisibility> decoderVisibilities(std::span<const DecoderConfig> configs) {
  std::vector<waveform_view::DecoderVisibility> all;
  all.reserve(configs.size());
  for (const DecoderConfig& config : configs) all.push_back(config.visibility());
  return all;
}

std::expected<void, std::string> applyDecoderVisibility(std::span<DecoderConfig> configs, std::string_view text,
                                                        const decode::DecoderRegistry& registry) {
  for (DecoderConfig& config : configs) config.visibility() = {};
  auto saved = waveform_view::parseDecoderVisibility(text);
  if (!saved) return std::unexpected(std::move(saved.error()));
  for (DecoderConfig& config : configs) {
    const auto found = saved->find(config.id());
    if (found == saved->end()) continue;
    auto decoder = registry.create(config.decoderName());
    if (!decoder) continue;
    config.visibility() = waveform_view::declaredVisibility(found->second, (*decoder)->rows(),
                                                            (*decoder)->annotationClasses());
  }
  return {};
}

}
