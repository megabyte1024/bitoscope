/*
 * SPDX-License-Identifier: MIT
 */

#include "decoder_pairing.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>

#include "channel_colors.h"
#include "decoder_chain_config.h"

namespace bitoscope::gui_decode {

namespace decode = bitoscope::decode;

namespace {

waveform_view::ColorChoice paletteColorAt(std::uint32_t counter) {
  return waveform_view::ColorChoice::palette(counter % waveform_view::kChannelPaletteSize);
}

bool readsDecoder(const DecoderConfig& reader, std::string_view inputName, const std::string& lowerId) {
  const std::optional<std::string> binding = reader.inputBinding(inputName);
  return binding && binding->substr(0, binding->find('.')) == lowerId;
}

}

std::map<std::string, std::string, std::less<>> autoPairInputs(std::span<const DecoderConfig> configs,
                                                               std::size_t newIndex,
                                                               const decode::DecoderRegistry& registry) {
  std::map<std::string, std::string, std::less<>> out;
  if (newIndex >= configs.size()) return out;
  auto created = registry.create(configs[newIndex].decoderName());
  if (!created) return out;
  const decode::Decoder& added = **created;
  if (added.protocols().empty()) return out;

  for (const decode::InputSpec& input : added.inputs()) {
    std::optional<std::pair<std::size_t, std::string>> chosen;
    for (const BindingCandidate& c : bindingCandidates(configs, newIndex, input.name, registry, Availability::Declared)) {
      if (!c.carries || c.carries->empty() || c.notDesignedFor) continue;
      if (chosen && chosen->first >= c.config) continue;
      bool used = false;
      for (std::size_t k = 0; k < configs.size() && !used; ++k) {
        used = k != newIndex && configs[k].decoderName() == configs[newIndex].decoderName() &&
               readsDecoder(configs[k], input.name, c.id);
      }
      if (!used) chosen = std::pair{c.config, c.binding};
    }
    if (chosen) out.emplace(input.name, std::move(chosen->second));
  }
  return out;
}

namespace {

bool insertLowerDecoder(std::vector<DecoderConfig>& configs, std::size_t at, const decode::InputSpec& input,
                        const std::vector<std::string>& protocols, const decode::DecoderRegistry& registry,
                        std::uint32_t* nextColor) {
  for (const std::string& protocol : protocols) {
    std::vector<std::string> names = registry.names();
    std::stable_partition(names.begin(), names.end(), [&](const std::string& n) { return n == protocol; });
    for (const std::string& name : names) {
      auto created = registry.create(name);
      if (!created || !(*created)->inputs().empty()) continue;
      const std::vector<decode::OutputSpec> outputs = (*created)->outputs();
      if (std::none_of(outputs.begin(), outputs.end(),
                       [&](const decode::OutputSpec& o) { return o.carries == protocol; })) {
        continue;
      }
      DecoderConfig lower(name);
      lower.setGeneratedId(uniqueDecoderId(name, configs));
      if (nextColor) lower.setColor(paletteColorAt(*nextColor));
      configs.insert(configs.begin() + static_cast<std::ptrdiff_t>(at), std::move(lower));
      if (autoPairInputs(configs, at + 1, registry).contains(input.name)) {
        if (nextColor) ++*nextColor;
        return true;
      }
      configs.erase(configs.begin() + static_cast<std::ptrdiff_t>(at));
    }
  }
  return false;
}

}

std::size_t addDecoder(std::vector<DecoderConfig>& configs, const std::string& decoderName,
                       const decode::DecoderRegistry& registry, std::uint32_t* nextColor) {
  DecoderConfig config(decoderName);
  config.setGeneratedId(uniqueDecoderId(decoderName, configs));
  if (nextColor) config.setColor(paletteColorAt((*nextColor)++));
  configs.push_back(std::move(config));
  std::size_t index = configs.size() - 1;
  if (auto created = registry.create(decoderName); created && !(*created)->protocols().empty()) {
    const std::vector<std::string> protocols = (*created)->protocols();
    for (const decode::InputSpec& input : (*created)->inputs()) {
      if (autoPairInputs(configs, index, registry).contains(input.name)) continue;
      if (insertLowerDecoder(configs, index, input, protocols, registry, nextColor)) ++index;
    }
  }
  for (const auto& [input, binding] : autoPairInputs(configs, index, registry)) {
    configs[index].setInputBinding(input, binding);
  }
  return index;
}

}
