/*
 * SPDX-License-Identifier: MIT
 */

#include "decoder_config.h"

#include <algorithm>
#include <cctype>
#include <utility>

#include "spec_display.h"

namespace bitoscope::gui_decode {

namespace decode = bitoscope::decode;

std::string channelFieldKey(std::string_view channelName) { return "channel:" + std::string(channelName); }
std::string inputFieldKey(std::string_view inputName) { return "input:" + std::string(inputName); }
std::string optionFieldKey(std::string_view optionName) { return "option:" + std::string(optionName); }
std::string idFieldKey() { return "id"; }

DecoderConfig::DecoderConfig(std::string decoderName) : decoderName_(std::move(decoderName)), id_(decoderName_) {}

DecoderConfig::DecoderConfig(std::string decoderName, std::string id)
    : decoderName_(std::move(decoderName)), id_(std::move(id)), idExplicit_(true) {}

std::vector<std::uint32_t> DecoderConfig::boundDeviceChannels() const {
  std::vector<std::uint32_t> channels;
  for (const auto& entry : channelBindings_) channels.push_back(entry.second);
  std::sort(channels.begin(), channels.end());
  channels.erase(std::unique(channels.begin(), channels.end()), channels.end());
  return channels;
}

std::optional<std::uint32_t> DecoderConfig::channelBinding(std::string_view channelName) const {
  const auto it = channelBindings_.find(channelName);
  if (it == channelBindings_.end()) return std::nullopt;
  return it->second;
}

void DecoderConfig::setChannelBinding(std::string_view channelName, std::optional<std::uint32_t> deviceChannel) {
  if (deviceChannel.has_value()) {
    channelBindings_[std::string(channelName)] = *deviceChannel;
  } else {
    channelBindings_.erase(std::string(channelName));
  }
}

std::optional<std::string> DecoderConfig::inputBinding(std::string_view inputName) const {
  const auto it = inputBindings_.find(inputName);
  if (it == inputBindings_.end()) return std::nullopt;
  return it->second;
}

void DecoderConfig::setInputBinding(std::string_view inputName, std::optional<std::string> binding) {
  if (binding.has_value()) {
    inputBindings_[std::string(inputName)] = std::move(*binding);
  } else {
    inputBindings_.erase(std::string(inputName));
  }
}

std::optional<waveform_view::ColorChoice> DecoderConfig::classColor(std::uint32_t rowId, std::uint32_t classId) const {
  const auto it = classColors_.find({rowId, classId});
  if (it == classColors_.end()) return std::nullopt;
  return it->second;
}

void DecoderConfig::setClassColor(std::uint32_t rowId, std::uint32_t classId,
                                  std::optional<waveform_view::ColorChoice> color) {
  if (color.has_value()) {
    classColors_[{rowId, classId}] = *color;
  } else {
    classColors_.erase({rowId, classId});
  }
}

decode::DataFormat DecoderConfig::rowDataFormat(std::uint32_t rowId) const {
  const auto it = rowDataFormats_.find(rowId);
  return it == rowDataFormats_.end() ? decode::DataFormat::Hex : it->second;
}

bool DecoderConfig::setRowDataFormat(std::uint32_t rowId, decode::DataFormat format) {
  if (rowDataFormat(rowId) == format) return false;
  if (format == decode::DataFormat::Hex) {
    rowDataFormats_.erase(rowId);
  } else {
    rowDataFormats_[rowId] = format;
  }
  return true;
}

std::string DecoderConfig::optionValue(std::string_view optionName, const decode::OptionSpec& spec) const {
  const auto it = optionValues_.find(optionName);
  return it == optionValues_.end() ? spec.default_value : it->second;
}

void DecoderConfig::setOptionValue(std::string_view optionName, std::string value) {
  optionValues_[std::string(optionName)] = std::move(value);
}

void DecoderConfig::recordError(const std::string& message, std::string_view why, const decode::Decoder* decoder) {
  errors_.clear();
  bool attributed = false;
  const auto names = [&](const std::string& text) { return why.find(text) != std::string_view::npos; };
  const auto mark = [&](std::string fieldKey) {
    errors_[std::move(fieldKey)] = message;
    attributed = true;
  };
  const auto namesItem = [&](const std::string& key) { return names("'" + key + "="); };

  if (why.starts_with("id '") || namesItem("id")) mark(idFieldKey());
  if (decoder != nullptr) {
    for (const decode::ChannelSpec& spec : decoder->channels()) {
      if (names("channel '" + spec.name + "'") || namesItem(spec.name)) mark(channelFieldKey(spec.name));
    }
    for (const decode::InputSpec& spec : decoder->inputs()) {
      bool about = names("input '" + spec.name + "'") || namesItem(spec.name);
      const auto bound = inputBindings_.find(spec.name);
      if (!about && bound != inputBindings_.end() && !bound->second.empty()) {
        const std::string& value = bound->second;
        const std::size_t dot = value.find('.');
        about = names(value) || names("'" + value.substr(0, dot) + "'") ||
                (dot != std::string::npos && names("output '" + value.substr(dot + 1) + "'"));
      }
      if (about) mark(inputFieldKey(spec.name));
    }
    for (const decode::OptionSpec& spec : decoder->options()) {
      if (names("option '" + spec.name + "'") || namesItem(spec.name)) mark(optionFieldKey(spec.name));
    }
  }
  if (!attributed) errors_[""] = message;
}

std::expected<std::unique_ptr<decode::Decoder>, std::string> DecoderConfig::validate(
    const decode::DecoderRegistry& registry, std::span<const CapturedChannel> captured) {
  auto decoder = registry.create(decoderName_);
  if (!decoder) {
    recordError(decoder.error(), decoder.error(), nullptr);
    return std::unexpected(decoder.error());
  }

  std::vector<decode::CapturedChannel> capturedForSpec;
  capturedForSpec.reserve(captured.size());
  for (const CapturedChannel& c : captured) capturedForSpec.push_back({c.deviceChannel, c.name});

  decode::DecoderSpec spec;
  spec.name = decoderName_;
  for (const decode::ChannelSpec& channelSpec : (*decoder)->channels()) {
    const auto it = channelBindings_.find(channelSpec.name);
    if (it != channelBindings_.end()) spec.settings.emplace_back(channelSpec.name, std::to_string(it->second));
  }
  for (const decode::OptionSpec& optionSpec : (*decoder)->options()) {
    const auto it = optionValues_.find(optionSpec.name);
    if (it != optionValues_.end()) spec.settings.emplace_back(optionSpec.name, it->second);
  }

  auto configured = decode::configureFromSpec(**decoder, spec, capturedForSpec);
  if (!configured) {
    recordError(configured.error(), configured.error(), decoder->get());
    return std::unexpected(configured.error());
  }
  errors_.clear();
  return std::move(*decoder);
}

std::optional<std::string> DecoderConfig::errorFor(std::string_view fieldKey) const {
  const auto it = errors_.find(fieldKey);
  if (it == errors_.end()) return std::nullopt;
  return it->second;
}

std::string uniqueDecoderId(std::string_view decoderName, std::span<const DecoderConfig> configs) {
  const auto taken = [&](const std::string& id) {
    for (const DecoderConfig& config : configs) {
      if (config.id() == id) return true;
      for (const auto& [input, binding] : config.inputBindings()) {
        if (binding.substr(0, binding.find('.')) == id) return true;
      }
    }
    return false;
  };
  std::string id(decoderName);
  for (unsigned n = 2; taken(id); ++n) id = std::string(decoderName) + "-" + std::to_string(n);
  return id;
}

std::expected<DecoderConfig, std::string> decoderConfigFromSetup(const decode::DecoderRegistry& registry,
                                                                  const std::string& name,
                                                                  const std::string& specRest,
                                                                  std::span<DecoderConfig> earlier,
                                                                  std::span<const CapturedChannel> captured) {
  auto decoder = registry.create(name);
  if (!decoder) return std::unexpected(decoder.error());

  DecoderConfig config(name);
  bool hasId = false;
  if (!specRest.empty()) {
    auto spec = decode::parseDecoderSpec(name + ":" + specRest);
    if (!spec) return std::unexpected(spec.error());

    const std::vector<decode::ChannelSpec> channelSpecs = (*decoder)->channels();
    const std::vector<decode::InputSpec> inputSpecs = (*decoder)->inputs();
    const std::vector<decode::OptionSpec> optionSpecs = (*decoder)->options();
    for (const auto& [key, value] : spec->settings) {
      if (key == "id") {
        config.setId(value);
        hasId = true;
        continue;
      }
      const bool isChannel = std::any_of(channelSpecs.begin(), channelSpecs.end(),
                                         [&](const decode::ChannelSpec& c) { return c.name == key; });
      if (isChannel) {
        const bool numeric = !value.empty() && std::all_of(value.begin(), value.end(), [](unsigned char c) {
          return std::isdigit(c) != 0;
        });
        if (numeric) {
          config.setChannelBinding(key, static_cast<std::uint32_t>(std::stoul(value)));
        } else {
          const auto named = std::find_if(captured.begin(), captured.end(),
                                          [&](const CapturedChannel& c) { return c.name == value; });
          if (named != captured.end()) config.setChannelBinding(key, named->deviceChannel);
        }
        continue;
      }
      const bool isInput = std::any_of(inputSpecs.begin(), inputSpecs.end(),
                                       [&](const decode::InputSpec& i) { return i.name == key; });
      if (isInput) {
        config.setInputBinding(key, value);
        continue;
      }
      const bool isOption = std::any_of(optionSpecs.begin(), optionSpecs.end(),
                                        [&](const decode::OptionSpec& o) { return o.name == key; });
      if (isOption) config.setOptionValue(key, value);
      // WHY: A newer file's field this build does not know is skipped, not failed.
    }
  }
  const auto namesId = [&](const std::string& id) {
    if (hasId && config.id() == id) return true;
    for (const auto& [input, binding] : config.inputBindings()) {
      if (binding.substr(0, binding.find('.')) == id) return true;
    }
    return false;
  };
  for (DecoderConfig& before : earlier) {
    if (before.idExplicit() || before.id() == before.decoderName() || !namesId(before.id())) continue;
    std::vector<DecoderConfig> all(earlier.begin(), earlier.end());
    all.push_back(config);
    before.setGeneratedId(uniqueDecoderId(before.decoderName(), all));
  }
  const bool taken = std::any_of(earlier.begin(), earlier.end(), [&](const DecoderConfig& c) { return c.id() == name; });
  if (!hasId && taken) config.setGeneratedId(uniqueDecoderId(name, earlier));
  return config;
}

bool DecoderConfig::writesId(std::span<const DecoderConfig> list) const {
  if (idExplicit_) return true;
  if (id_ == decoderName_) return false;
  for (const DecoderConfig& config : list) {
    for (const auto& [input, binding] : config.inputBindings_) {
      const std::string named = binding.substr(0, binding.find('.'));
      if (named == id_ || named == decoderName_) return true;
    }
  }
  return false;
}

std::string DecoderConfig::toSpecString(const decode::Decoder& decoder) const { return toSpecString(decoder, {}); }

std::string DecoderConfig::toSpecString(const decode::Decoder& decoder, std::span<const DecoderConfig> list) const {
  std::string out;
  const auto append = [&](std::string_view key, std::string_view value) {
    if (!out.empty()) out += ':';
    out += key;
    out += '=';
    out += value;
  };
  if (writesId(list)) append("id", id_);
  for (const decode::ChannelSpec& spec : decoder.channels()) {
    const auto it = channelBindings_.find(spec.name);
    if (it != channelBindings_.end()) append(spec.name, std::to_string(it->second));
  }
  for (const decode::InputSpec& spec : decoder.inputs()) {
    const auto it = inputBindings_.find(spec.name);
    if (it != inputBindings_.end()) append(spec.name, it->second);
  }
  for (const decode::OptionSpec& spec : decoder.options()) {
    const auto it = optionValues_.find(spec.name);
    if (it != optionValues_.end()) append(spec.name, it->second);
  }
  return out;
}

DecoderConfig::AnnotationFormatContext DecoderConfig::annotationFormatContext(
    const decode::DecoderRegistry& registry) const {
  AnnotationFormatContext context;
  auto decoder = registry.create(decoderName_);
  if (!decoder) return context;
  context.classes = decode::displayClassesOf(**decoder);
  for (const decode::OptionSpec& spec : (*decoder)->options()) {
    context.options[spec.name] = optionValue(spec.name, spec);
  }
  return context;
}

}
