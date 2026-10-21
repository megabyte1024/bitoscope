/*
 * SPDX-License-Identifier: MIT
 */

#include "decoder_chain_config.h"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <format>
#include <memory>
#include <optional>
#include <utility>

#include "decoder_labels.h"
#include "derivation_context.h"
#include "protocol_fit.h"
#include "stream.h"

namespace bitoscope::gui_decode {

namespace decode = bitoscope::decode;

namespace {

std::string bindingId(const std::string& binding) { return binding.substr(0, binding.find('.')); }

std::string specText(const DecoderConfig& config, const decode::Decoder* decoder, std::span<const DecoderConfig> list) {
  const std::string rest = decoder != nullptr      ? config.toSpecString(*decoder, list)
                           : config.writesId(list) ? "id=" + config.id()
                                                   : std::string();
  return rest.empty() ? config.decoderName() : config.decoderName() + ":" + rest;
}

std::string_view reason(const std::string& message, std::string_view prefix) {
  std::string_view why(message);
  if (why.starts_with(prefix)) why.remove_prefix(prefix.size());
  return why;
}

std::string renumbered(const std::string& message, std::string_view prefix, std::span<const std::size_t> specConfig,
                       std::span<const DecoderConfig> configs, const decode::DecoderRegistry& registry) {
  const std::string_view why = reason(message, prefix);
  std::string out(message.substr(0, message.size() - why.size()));
  std::size_t at = 0;
  for (std::size_t p = why.find("-P "); p != std::string_view::npos; p = why.find("-P ", at)) {
    const std::size_t digits = p + 3;
    std::size_t end = digits;
    while (end < why.size() && why[end] >= '0' && why[end] <= '9') ++end;
    std::size_t n = 0;
    const auto parsed = std::from_chars(why.data() + digits, why.data() + end, n);
    if (end == digits || parsed.ec != std::errc() || n == 0 || n > specConfig.size()) {
      out.append(why.substr(at, end - at));
      at = end;
      continue;
    }
    const std::size_t config = specConfig[n - 1];
    out.append(why.substr(at, p - at));
    out += decoderListName(configs, config, registry);
    at = end;
  }
  out.append(why.substr(at));
  return out;
}

}

ValidatedChain validateAll(std::span<DecoderConfig> configs, const decode::DecoderRegistry& registry,
                           std::span<const CapturedChannel> captured, const decode::CaptureFacts& facts) {
  std::vector<decode::CapturedChannel> capturedForPlan;
  capturedForPlan.reserve(captured.size());
  for (const CapturedChannel& c : captured) capturedForPlan.push_back({c.deviceChannel, c.name});

  ValidatedChain out;
  std::vector<std::string> accepted;
  std::vector<bool> valid(configs.size(), false);
  for (std::size_t i = 0; i < configs.size(); ++i) {
    DecoderConfig& config = configs[i];
    auto created = registry.create(config.decoderName());
    const decode::Decoder* decoder = created ? created->get() : nullptr;
    const std::string text = specText(config, decoder, configs);
    const std::string prefix = std::format("-P '{}': ", text);

    std::optional<std::string> unavailable;
    if (decoder != nullptr) {
      for (const decode::InputSpec& spec : decoder->inputs()) {
        const std::optional<std::string> binding = config.inputBinding(spec.name);
        if (!binding || unavailable) continue;
        const std::string id = bindingId(*binding);
        bool named = false;
        bool namedValid = false;
        for (std::size_t j = 0; j < i; ++j) {
          if (configs[j].id() != id) continue;
          named = true;
          namedValid = namedValid || valid[j];
        }
        if (named && !namedValid) {
          unavailable = std::format("{}input '{}' ({}) is not available: decoder '{}' is not valid", prefix,
                                    spec.name, *binding, id);
        }
      }
    }
    if (unavailable) {
      config.recordError(*unavailable, reason(*unavailable, prefix), decoder);
      continue;
    }

    std::vector<std::string> specs = accepted;
    specs.push_back(text);
    std::vector<std::size_t> specConfig = out.configIndices;
    specConfig.push_back(i);
    auto planned = decode::planChain(specs, registry, capturedForPlan, facts);
    if (!planned) {
      std::string message = planned.error();
      std::vector<std::string> later;
      std::vector<std::size_t> laterConfig;
      for (const auto& [input, binding] : config.inputBindings()) {
        const std::string id = bindingId(binding);
        const bool above = std::any_of(configs.begin(), configs.begin() + static_cast<std::ptrdiff_t>(i),
                                       [&](const DecoderConfig& c) { return c.id() == id; });
        if (above) continue;
        for (std::size_t j = i + 1; j < configs.size(); ++j) {
          if (configs[j].id() != id) continue;
          later.push_back(configs[j].decoderName() + ":id=" + id);
          laterConfig.push_back(j);
        }
      }
      if (!later.empty()) {
        specs.insert(specs.end(), later.begin(), later.end());
        auto replanned = decode::planChain(specs, registry, capturedForPlan, facts);
        if (!replanned && replanned.error().starts_with(prefix)) {
          message = replanned.error();
          specConfig.insert(specConfig.end(), laterConfig.begin(), laterConfig.end());
        }
      }
      message = renumbered(message, prefix, specConfig, configs, registry);
      config.recordError(message, reason(message, prefix), decoder);
      continue;
    }
    config.clearErrors();
    valid[i] = true;
    accepted.push_back(text);
    out.configIndices.push_back(i);
    out.plan = std::move(*planned);
  }
  return out;
}

ValidatedChain validateForRun(std::span<DecoderConfig> configs, const decode::DecoderRegistry& registry,
                              std::span<const CapturedChannel> captured, const decode::CaptureFacts& facts) {
  ValidatedChain chain = validateAll(configs, registry, captured, facts);
  std::vector<std::string> texts;
  texts.reserve(chain.configIndices.size());
  bool forced = false;
  for (const std::size_t i : chain.configIndices) {
    auto created = registry.create(configs[i].decoderName());
    const decode::Decoder* decoder = created ? created->get() : nullptr;
    DecoderConfig run = configs[i];
    if (decoder != nullptr) {
      for (const std::string& option : derivedRowEnableOptions(*decoder)) {
        run.setOptionValue(option, "no");
        forced = true;
      }
    }
    texts.push_back(specText(run, decoder, configs));
  }
  if (!forced) return chain;
  std::vector<decode::CapturedChannel> capturedForPlan;
  capturedForPlan.reserve(captured.size());
  for (const CapturedChannel& c : captured) capturedForPlan.push_back({c.deviceChannel, c.name});
  auto planned = decode::planChain(texts, registry, capturedForPlan, facts);
  if (planned) chain.plan = std::move(*planned);
  return chain;
}

namespace {

void bindProducers(std::span<const DecoderConfig> configs, std::span<std::unique_ptr<decode::Decoder>> producers) {
  for (std::size_t j = 0; j < producers.size(); ++j) {
    if (!producers[j]) continue;
    decode::Decoder& decoder = *producers[j];
    decode::InputMap inputs;
    for (const auto& [name, binding] : configs[j].inputBindings()) {
      const std::string id = bindingId(binding);
      for (std::size_t k = 0; k < j; ++k) {
        if (configs[k].id() != id || !producers[k]) continue;
        const std::vector<decode::OutputSpec> outputs = producers[k]->outputs();
        if (binding.size() <= id.size()) break;
        const std::string outputName = binding.substr(id.size() + 1);
        const auto out = std::find_if(outputs.begin(), outputs.end(),
                                      [&](const decode::OutputSpec& o) { return o.name == outputName; });
        if (out == outputs.end()) break;
        inputs.emplace(name, decode::BoundInput{binding, *out, producers[k]->resolvedWidth(*out),
                                                producers[k]->widthNote(*out), producers[k].get()});
        break;
      }
    }
    decode::ChannelMap channels;
    for (const decode::ChannelSpec& spec : decoder.channels()) {
      if (const auto bound = configs[j].channelBinding(spec.name)) channels[spec.name] = *bound;
    }
    decode::OptionMap options;
    for (const decode::OptionSpec& spec : decoder.options()) {
      options[spec.name] = configs[j].optionValue(spec.name, spec);
    }
    (void)decoder.configure(channels, options, inputs);
  }
}

}

std::vector<BindingCandidate> bindingCandidates(std::span<const DecoderConfig> configs, std::size_t index,
                                                std::string_view inputName, const decode::DecoderRegistry& registry,
                                                Availability availability) {
  std::vector<BindingCandidate> out;
  if (index >= configs.size()) return out;
  auto reader = registry.create(configs[index].decoderName());
  if (!reader) return out;
  const std::vector<decode::InputSpec> inputs = (*reader)->inputs();
  const auto input = std::find_if(inputs.begin(), inputs.end(),
                                  [&](const decode::InputSpec& s) { return s.name == inputName; });
  if (input == inputs.end()) return out;

  std::vector<std::unique_ptr<decode::Decoder>> producers(index);
  bool passesThrough = false;
  for (std::size_t j = 0; j < index; ++j) {
    auto decoder = registry.create(configs[j].decoderName());
    if (!decoder) continue;
    for (const decode::OutputSpec& output : (*decoder)->outputs()) passesThrough = passesThrough || output.carries_input;
    producers[j] = std::move(*decoder);
  }
  if (passesThrough) bindProducers(configs, producers);

  for (std::size_t j = 0; j < index; ++j) {
    const DecoderConfig& source = configs[j];
    if (!producers[j]) continue;
    const decode::Decoder& decoder = *producers[j];
    const std::vector<decode::OptionSpec> options = decoder.options();
    for (const decode::OutputSpec& output : decoder.outputs()) {
      if (availability == Availability::ChannelsBound && !output.requires_channel.empty() &&
          !source.channelBinding(output.requires_channel)) {
        continue;
      }
      std::uint32_t width = output.width;
      std::string widthNote;
      if (!output.width_option.empty()) {
        const auto option = std::find_if(options.begin(), options.end(),
                                         [&](const decode::OptionSpec& o) { return o.name == output.width_option; });
        if (option == options.end()) continue;
        const std::string value = source.optionValue(option->name, *option);
        const auto [end, ec] = std::from_chars(value.data(), value.data() + value.size(), width);
        if (ec != std::errc() || end != value.data() + value.size()) continue;
        widthNote = std::format("{}={}", option->name, value);
      }
      const std::string binding = source.id() + "." + output.name;
      if (!decode::checkBinding((*reader)->name(), *input, binding, output, width, widthNote)) continue;
      BindingCandidate candidate{j, source.id(), output.name, binding, decode::resolveOutputCarries(decoder, output),
                                 std::nullopt};
      if (candidate.carries && !decode::protocolAllows(**reader, *candidate.carries)) {
        if (auto fits = decode::checkProtocolFit(**reader, binding, decoder, output); !fits) {
          candidate.notDesignedFor = std::move(fits.error());
        }
      }
      out.push_back(std::move(candidate));
    }
  }
  return out;
}

std::vector<BindingCandidate> dropdownCandidates(std::span<const DecoderConfig> configs, std::size_t index,
                                                 std::string_view inputName, const decode::DecoderRegistry& registry) {
  if (index >= configs.size()) return {};
  auto reader = registry.create(configs[index].decoderName());
  const bool designedFor = reader && !(*reader)->protocols().empty();
  return bindingCandidates(configs, index, inputName, registry,
                           designedFor ? Availability::Declared : Availability::ChannelsBound);
}

std::vector<std::size_t> laneOrder(std::span<const DecoderConfig> configs) {
  const std::size_t n = configs.size();
  std::vector<std::vector<std::size_t>> readers(n);
  std::vector<bool> root(n, true);
  for (std::size_t i = 0; i < n; ++i) {
    for (const auto& [input, binding] : configs[i].inputBindings()) {
      const std::string id = bindingId(binding);
      const auto lower = std::find_if(configs.begin(), configs.begin() + static_cast<std::ptrdiff_t>(i),
                                      [&](const DecoderConfig& c) { return c.id() == id; });
      if (lower == configs.begin() + static_cast<std::ptrdiff_t>(i)) continue;
      readers[static_cast<std::size_t>(lower - configs.begin())].push_back(i);
      root[i] = false;
      break;
    }
  }
  // WHY: Depth first terminates: a reader is always below its lower decoder, so there is no cycle.
  std::vector<std::size_t> order;
  order.reserve(n);
  std::vector<std::size_t> stack;
  for (std::size_t r = 0; r < n; ++r) {
    if (!root[r]) continue;
    stack.push_back(r);
    while (!stack.empty()) {
      const std::size_t at = stack.back();
      stack.pop_back();
      order.push_back(at);
      stack.insert(stack.end(), readers[at].rbegin(), readers[at].rend());
    }
  }
  return order;
}

DecoderReads decoderReads(std::span<const DecoderConfig> configs, std::size_t index) {
  DecoderReads reads;
  if (index >= configs.size()) return reads;
  reads.channels = configs[index].boundDeviceChannels();
  for (const auto& [input, binding] : configs[index].inputBindings()) {
    const std::string id = bindingId(binding);
    const auto above = configs.begin() + static_cast<std::ptrdiff_t>(index);
    const auto lower = std::find_if(configs.begin(), above, [&](const DecoderConfig& c) { return c.id() == id; });
    if (lower == above) continue;
    const std::size_t lowerIndex = static_cast<std::size_t>(lower - configs.begin());
    if (std::ranges::find(reads.decoders, lowerIndex) == reads.decoders.end()) reads.decoders.push_back(lowerIndex);
  }
  return reads;
}

std::expected<void, std::string> renameDecoder(std::span<DecoderConfig> configs, std::size_t index,
                                               std::string_view newId, const decode::DecoderRegistry& registry) {
  if (index >= configs.size()) {
    return std::unexpected(std::format("no decoder #{}: list holds {}", index, configs.size()));
  }
  const std::string oldId = configs[index].id();
  if (newId == oldId) return {};
  if (newId.empty()) return std::unexpected(std::string("id is empty"));
  if (!decode::isValidDecoderId(newId)) {
    return std::unexpected(std::format(
        "id '{}' is not valid: it starts with letter or '_' and has only letters, digits, '_' and '-'", newId));
  }
  for (std::size_t j = 0; j < configs.size(); ++j) {
    if (j != index && configs[j].id() == newId) {
      return std::unexpected(
          std::format("id '{}' is already given to {}", newId, decoderListName(configs, j, registry)));
    }
  }
  for (std::size_t j = 0; j < configs.size(); ++j) {
    if (j != index && !configs[j].idExplicit() && configs[j].decoderName() == newId) {
      return std::unexpected(std::format("id '{}' is default id of {}, which has no id of its own", newId,
                                         decoderListName(configs, j, registry)));
    }
  }
  struct Rewrite {
    std::size_t config;
    std::string input;
    std::string binding;
  };
  std::vector<Rewrite> rewrites;
  for (std::size_t k = 0; k < configs.size(); ++k) {
    for (const auto& [input, binding] : configs[k].inputBindings()) {
      if (bindingId(binding) != oldId) continue;
      rewrites.push_back({k, input, std::string(newId) + binding.substr(oldId.size())});
    }
  }
  std::string id(newId);
  for (Rewrite& r : rewrites) configs[r.config].setInputBinding(r.input, std::move(r.binding));
  configs[index].setId(std::move(id));
  return {};
}

std::vector<std::pair<std::string, std::string>> decoderSetupOf(std::span<const DecoderConfig> configs,
                                                                const decode::DecoderRegistry& registry) {
  std::vector<std::pair<std::string, std::string>> out;
  for (const DecoderConfig& config : configs) {
    auto decoder = registry.create(config.decoderName());
    if (decoder) out.emplace_back(config.decoderName(), config.toSpecString(**decoder, configs));
  }
  return out;
}

}
