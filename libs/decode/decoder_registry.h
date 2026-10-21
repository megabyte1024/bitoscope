/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <expected>
#include <format>
#include <functional>
#include <optional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "decoder.h"

namespace bitoscope::decode {

struct DecoderSpec {
  std::string name;
  std::vector<std::pair<std::string, std::string>> settings;
};

inline std::expected<DecoderSpec, std::string> parseDecoderSpec(std::string_view text) {
  if (text.empty()) return std::unexpected("decoder spec is empty");
  for (const char c : text) {
    if (std::isspace(static_cast<unsigned char>(c)) != 0) {
      return std::unexpected(std::format("decoder spec '{}' contains whitespace", text));
    }
    if (c == ',') {
      return std::unexpected(std::format(
          "'{}': comma does not stack decoders; give each decoder its own -P and bind upper one's "
          "input with in=ID.OUTPUT, e.g. -P uart:id=link:rx=D1 -P cobs:in=link.rx",
          text));
    }
  }

  DecoderSpec spec;
  size_t pos = text.find(':');
  spec.name = std::string(text.substr(0, pos));
  if (spec.name.empty()) return std::unexpected("decoder spec has no decoder name");

  while (pos != std::string_view::npos) {
    const size_t start = pos + 1;
    pos = text.find(':', start);
    const std::string_view item = text.substr(start, pos == std::string_view::npos ? pos : pos - start);
    if (item.empty()) return std::unexpected("decoder spec has empty item (stray ':')");
    const size_t eq = item.find('=');
    if (eq == std::string_view::npos) {
      return std::unexpected(std::format("'{}' needs value: write {}=VALUE", item, item));
    }
    std::string key(item.substr(0, eq));
    std::string value(item.substr(eq + 1));
    if (key.empty()) return std::unexpected(std::format("'{}' has no key before '='", item));
    if (value.empty()) return std::unexpected(std::format("'{}' has no value after '='", item));
    for (const auto& existing : spec.settings) {
      if (existing.first == key) {
        return std::unexpected(std::format("'{}' is given twice", key));
      }
    }
    spec.settings.emplace_back(std::move(key), std::move(value));
  }
  return spec;
}

struct CapturedChannel {
  uint32_t device_index = 0;
  std::string name;
};

inline std::expected<void, std::string> configureFromSpec(Decoder& decoder, const DecoderSpec& spec,
                                                          std::span<const CapturedChannel> captured,
                                                          const InputMap& inputs = {}) {
  const std::vector<ChannelSpec> channel_specs = decoder.channels();
  const std::vector<InputSpec> input_specs = decoder.inputs();
  ChannelMap channels;
  OptionMap options;
  for (const auto& [key, value] : spec.settings) {
    if (key == "id") continue;
    if (std::any_of(input_specs.begin(), input_specs.end(), [&](const InputSpec& s) { return s.name == key; })) {
      continue;
    }
    const bool is_channel = std::any_of(channel_specs.begin(), channel_specs.end(),
                                        [&](const ChannelSpec& c) { return c.name == key; });
    if (!is_channel) {
      options[key] = value;
      continue;
    }
    const bool numeric = std::all_of(value.begin(), value.end(),
                                     [](unsigned char c) { return std::isdigit(c) != 0; });
    std::optional<uint32_t> position;
    for (size_t i = 0; i < captured.size(); ++i) {
      const bool match = numeric ? std::to_string(captured[i].device_index) == value
                                 : captured[i].name == value;
      if (match) {
        position = static_cast<uint32_t>(i);
        break;
      }
    }
    if (!position) {
      return std::unexpected(std::format("channel '{}' is given as '{}', which is not captured channel",
                                         key, value));
    }
    channels[key] = *position;
  }
  return decoder.configure(channels, options, inputs);
}

class DecoderRegistry {
 public:
  using Factory = std::function<std::unique_ptr<Decoder>()>;

  std::expected<void, std::string> add(std::string name, Factory factory) {
    if (name.empty()) return std::unexpected("decoder needs name");
    if (!factory) return std::unexpected(std::format("decoder '{}' has no factory", name));
    if (std::any_of(factories_.begin(), factories_.end(),
                    [&](const auto& f) { return f.first == name; })) {
      return std::unexpected(std::format("decoder '{}' is already registered", name));
    }
    factories_.emplace_back(std::move(name), std::move(factory));
    return {};
  }

  std::expected<std::unique_ptr<Decoder>, std::string> create(std::string_view name) const {
    for (const auto& [known, factory] : factories_) {
      if (known != name) continue;
      std::unique_ptr<Decoder> decoder = factory();
      if (auto declared = checkDeclaration(*decoder, MixedDeclarations::Allowed); !declared) {
        return std::unexpected(declared.error());
      }
      return decoder;
    }
    std::string list;
    for (const std::string& n : names()) list += (list.empty() ? "" : ", ") + n;
    return std::unexpected(std::format("unknown decoder '{}'; known decoders: {}", name,
                                       list.empty() ? "(none)" : list));
  }

  std::vector<std::string> names() const {
    std::vector<std::string> out;
    for (const auto& f : factories_) out.push_back(f.first);
    std::sort(out.begin(), out.end());
    return out;
  }

 private:
  std::vector<std::pair<std::string, Factory>> factories_;
};

}
