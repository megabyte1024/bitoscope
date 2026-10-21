/*
 * SPDX-License-Identifier: MIT
 */

#include "decoder_chain.h"

#include <algorithm>
#include <format>
#include <utility>

#include "chain_stepper.h"
#include "protocol_fit.h"

namespace bitoscope::decode {

namespace {

constexpr std::string_view kIdKey = "id";

std::string listPositions(const std::vector<std::size_t>& indices) {
  std::string out;
  for (std::size_t i = 0; i < indices.size(); ++i) {
    if (i > 0) out += i + 1 == indices.size() ? " and " : ", ";
    out += std::format("-P {}", indices[i] + 1);
  }
  return out;
}

std::string joined(const std::vector<std::string>& names) {
  std::string out;
  for (const std::string& n : names) out += (out.empty() ? "" : ", ") + n;
  return out.empty() ? std::string("(none)") : out;
}

struct Parsed {
  DecoderSpec spec;
  std::string id;
  bool explicit_id = false;
};

}

bool isValidDecoderId(std::string_view id) {
  if (id.empty()) return false;
  const auto letter = [](char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_'; };
  if (!letter(id.front())) return false;
  return std::all_of(id.begin() + 1, id.end(),
                     [&](char c) { return letter(c) || (c >= '0' && c <= '9') || c == '-'; });
}

std::expected<ChainPlan, std::string> planChain(std::span<const std::string> specs, const DecoderRegistry& registry,
                                                std::span<const CapturedChannel> captured, const CaptureFacts& facts) {
  // WHY: Every id is parsed first so an input can tell an unknown id from one that is only given later.
  std::vector<Parsed> parsed;
  for (std::size_t i = 0; i < specs.size(); ++i) {
    const std::string& text = specs[i];
    const auto refuse = [&text](const std::string& why) {
      return std::unexpected(std::format("-P '{}': {}", text, why));
    };
    auto spec = parseDecoderSpec(text);
    if (!spec) return refuse(spec.error());
    Parsed p{std::move(*spec), {}, false};
    p.id = p.spec.name;
    for (const auto& [key, value] : p.spec.settings) {
      if (key != kIdKey) continue;
      if (!isValidDecoderId(value)) {
        return refuse(std::format("id '{}' is not valid: it starts with letter or '_' and has only letters, "
                                  "digits, '_' and '-'",
                                  value));
      }
      p.id = value;
      p.explicit_id = true;
    }
    if (p.explicit_id) {
      for (std::size_t j = 0; j < parsed.size(); ++j) {
        if (parsed[j].explicit_id && parsed[j].id == p.id) {
          return refuse(std::format("id '{}' is already given to -P {}", p.id, j + 1));
        }
      }
    }
    parsed.push_back(std::move(p));
  }

  ChainPlan plan;
  for (std::size_t i = 0; i < parsed.size(); ++i) {
    const std::string& text = specs[i];
    const auto refuse = [&text](const std::string& why) {
      return std::unexpected(std::format("-P '{}': {}", text, why));
    };
    const Parsed& p = parsed[i];
    auto created = registry.create(p.spec.name);
    if (!created) return refuse(created.error());
    std::unique_ptr<Decoder> decoder = std::move(*created);

    const std::vector<InputSpec> input_specs = decoder->inputs();
    InputMap inputs;
    std::vector<ChainBinding> bindings;
    for (const auto& [key, value] : p.spec.settings) {
      if (std::none_of(input_specs.begin(), input_specs.end(), [&](const InputSpec& s) { return s.name == key; })) {
        continue;
      }
      const std::size_t dot = value.find('.');
      if (dot == std::string::npos || dot == 0 || dot + 1 == value.size()) {
        return refuse(std::format("input '{}' is given '{}'; write {}=ID.OUTPUT", key, value, key));
      }
      const std::string id = value.substr(0, dot);
      const std::string output = value.substr(dot + 1);

      std::vector<std::size_t> matches;
      for (std::size_t j = 0; j < parsed.size(); ++j) {
        if (parsed[j].id == id) matches.push_back(j);
      }
      if (matches.empty()) {
        std::vector<std::string> known;
        for (std::size_t j = 0; j < i; ++j) {
          if (std::find(known.begin(), known.end(), parsed[j].id) == known.end()) known.push_back(parsed[j].id);
        }
        return refuse(std::format("unknown decoder id '{}'; known ids: {}", id, joined(known)));
      }
      if (matches.size() > 1) {
        return refuse(std::format("'{}' names {}; give one you mean id=", id, listPositions(matches)));
      }
      const std::size_t from = matches.front();
      if (from == i) return refuse(std::format("decoder '{}' cannot read its own output", id));
      if (from > i) {
        return refuse(
            std::format("decoder '{}' is given after this one; input must come from earlier -P", id));
      }

      const Decoder& source = *plan.instances[from].decoder;
      const std::vector<OutputSpec> outputs = source.outputs();
      const auto out = std::find_if(outputs.begin(), outputs.end(),
                                    [&](const OutputSpec& o) { return o.name == output; });
      if (out == outputs.end()) {
        std::vector<std::string> names;
        for (const OutputSpec& o : outputs) names.push_back(o.name);
        return refuse(std::format("decoder '{}' has no output '{}'; its outputs: {}", id, output, joined(names)));
      }
      if (!source.outputAvailable(*out)) {
        return refuse(std::format("{}'s output '{}' needs its channel '{}' bound", displayOf(source), output,
                                  out->requires_channel));
      }
      inputs.emplace(key, BoundInput{value, *out, source.resolvedWidth(*out), source.widthNote(*out), &source});
      bindings.push_back({key, from, output});
    }

    if (auto configured = configureFromSpec(*decoder, p.spec, captured, inputs); !configured) {
      return refuse(configured.error());
    }
    // WHY: Resolved here and not in configure(): what an output carries can pass through decoders below the binding,
    // which only the chain holds; a plugin's configure() rebuilds its inputs from the wire without that declaration.
    for (const ChainBinding& b : bindings) {
      const BoundInput& bound = inputs.at(b.input);
      if (auto fits = checkProtocolFit(*decoder, bound.source, *plan.instances[b.from].decoder, bound.output);
          !fits) {
        return refuse(fits.error());
      }
    }
    if (auto fits = decoder->checkCapture(facts); !fits) return refuse(fits.error());

    for (const ChainBinding& b : bindings) {
      std::vector<std::string>& wanted = plan.instances[b.from].wanted;
      if (std::find(wanted.begin(), wanted.end(), b.output) == wanted.end()) wanted.push_back(b.output);
    }
    plan.instances.push_back({text, p.id, std::move(decoder), std::move(bindings), {}});
  }

  for (ChainInstance& instance : plan.instances) {
    if (instance.wanted.size() < 2) continue;
    const std::vector<OutputSpec> outputs = instance.decoder->outputs();
    std::vector<std::string> ordered;
    for (const OutputSpec& o : outputs) {
      if (std::find(instance.wanted.begin(), instance.wanted.end(), o.name) != instance.wanted.end()) {
        ordered.push_back(o.name);
      }
    }
    instance.wanted = std::move(ordered);
  }
  return plan;
}

std::vector<ChainOutcome> runChain(std::span<ChainInstance> instances, const SignalView& view,
                                   const std::function<AnnotationSink&(std::size_t)>& sink_for,
                                   const ChainRunHooks& hooks) {
  ChainStepper stepper(instances, view, sink_for, hooks);
  std::vector<ChainOutcome> outcomes;
  outcomes.reserve(instances.size());
  for (std::size_t i = 0; i < instances.size(); ++i) {
    auto stepped = stepper.run(i);
    outcomes.push_back(stepped ? std::move(*stepped)
                               : ChainOutcome(std::unexpected(std::format(
                                     "decoder '{}' was not run: its input does not come from earlier decoder",
                                     instances[i].id))));
  }
  return outcomes;
}

std::vector<ChainOutcome> runChain(ChainPlan& plan, const SignalView& view,
                                   const std::function<AnnotationSink&(std::size_t)>& sink_for,
                                   const ChainRunHooks& hooks) {
  return runChain(std::span<ChainInstance>(plan.instances), view, sink_for, hooks);
}

}
