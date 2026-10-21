/*
 * SPDX-License-Identifier: MIT
 */

#include "decoder_labels.h"

#include <algorithm>
#include <format>

#include "display_text.h"
#include "spec_display.h"

namespace bitoscope::gui_decode {

namespace decode = bitoscope::decode;

namespace {

std::string starredIf(std::string label, bool required) { return required ? label + " *" : label; }

bool isOwnIdRest(std::string_view rest) {
  if (rest.empty()) return true;
  if (rest.size() < 2 || rest.front() != '-') return false;
  return std::all_of(rest.begin() + 1, rest.end(), [](char c) { return c >= '0' && c <= '9'; });
}

std::string ownDisplayName(std::string_view decoderName) {
  std::string text(decoderName);
  std::replace(text.begin(), text.end(), '_', ' ');
  return display_text::displayName(text);
}

}

std::string decoderDisplay(const decode::DecoderRegistry& registry, std::string_view decoderName) {
  const auto decoder = registry.create(decoderName);
  if (!decoder) return display_text::displayName(decoderName);
  return decode::displayOf(**decoder);
}

std::vector<std::string> decoderChoiceLabels(const decode::DecoderRegistry& registry) {
  std::vector<std::string> out;
  for (const std::string& name : registry.names()) out.push_back(decoderDisplay(registry, name));
  return out;
}

std::string decoderListName(std::span<const DecoderConfig> configs, std::size_t index,
                            const decode::DecoderRegistry& registry) {
  const DecoderConfig& config = configs[index];
  const auto kind = std::count_if(configs.begin(), configs.begin() + static_cast<std::ptrdiff_t>(index),
                                  [&](const DecoderConfig& c) { return c.decoderName() == config.decoderName(); });
  return std::format("{} #{}", decoderDisplay(registry, config.decoderName()), kind + 1);
}

std::string displayInstanceName(std::string_view id, std::string_view decoderName) {
  if (decoderName.empty() || !id.starts_with(decoderName) || !isOwnIdRest(id.substr(decoderName.size()))) {
    return std::string(id);
  }
  return ownDisplayName(decoderName) + std::string(id.substr(decoderName.size()));
}

std::string storedInstanceId(std::string_view typed, std::string_view decoderName) {
  const std::string shown = ownDisplayName(decoderName);
  if (decoderName.empty() || !typed.starts_with(shown) || !isOwnIdRest(typed.substr(shown.size()))) {
    return std::string(typed);
  }
  return std::string(decoderName) + std::string(typed.substr(shown.size()));
}

std::string decoderBarName(std::span<const DecoderConfig> configs, std::size_t index,
                           const decode::DecoderRegistry& registry) {
  const DecoderConfig& config = configs[index];
  const std::string_view own = config.decoderName();
  const bool givenByDecoder = config.id().starts_with(own) && isOwnIdRest(std::string_view(config.id()).substr(own.size()));
  return givenByDecoder ? decoderListName(configs, index, registry) : config.id();
}

std::string humanLabel(std::string_view id, std::string_view display) {
  if (!display.empty()) return display_text::displayName(display);
  std::string text(id);
  std::replace(text.begin(), text.end(), '_', ' ');
  std::string label = display_text::displayName(text);
  if (!label.empty() && label.front() >= 'a' && label.front() <= 'z') label.front() = static_cast<char>(label.front() - 'a' + 'A');
  return label;
}

std::string channelLabel(const decode::ChannelSpec& spec) {
  return starredIf(humanLabel(spec.name, spec.display), spec.required);
}
std::string inputLabel(const decode::InputSpec& spec) {
  return starredIf(humanLabel(spec.name, spec.display), spec.required);
}
std::string optionLabel(const decode::OptionSpec& spec) { return humanLabel(spec.name, spec.display); }

std::string channelTooltip(const decode::ChannelSpec& spec) { return spec.description; }
std::string optionTooltip(const decode::OptionSpec& spec) { return spec.description; }
std::string inputTooltip(const decode::InputSpec& spec) { return spec.description; }

std::string bindingLabel(std::string_view binding, std::span<const DecoderConfig> configs,
                         const decode::DecoderRegistry& registry) {
  const std::size_t dot = binding.find('.');
  if (dot == std::string_view::npos) return std::string(binding);
  const std::string_view id = binding.substr(0, dot);
  const std::string_view output = binding.substr(dot + 1);
  const auto config = std::find_if(configs.begin(), configs.end(), [&](const DecoderConfig& c) { return c.id() == id; });
  if (config == configs.end()) return std::string(binding);
  const auto decoder = registry.create(config->decoderName());
  if (!decoder) return std::string(binding);
  for (const decode::OutputSpec& spec : (*decoder)->outputs()) {
    if (spec.name == output) {
      return std::format("{}.{}", displayInstanceName(id, config->decoderName()), decode::displayOf(spec));
    }
  }
  return std::string(binding);
}

std::string decoderLaneLabel(const DecoderConfig& config, std::span<const DecoderConfig> configs,
                             const decode::DecoderRegistry& registry) {
  const std::string decoder = decoderDisplay(registry, config.decoderName());
  if (!config.inputBindings().empty()) {
    return std::format("{} <- {}", decoder, bindingLabel(config.inputBindings().begin()->second, configs, registry));
  }
  return config.id() == config.decoderName()
             ? decoder
             : std::format("{} ({})", decoder, displayInstanceName(config.id(), config.decoderName()));
}

}
