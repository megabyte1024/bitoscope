/*
 * SPDX-License-Identifier: MIT
 */

#include "derivation_context.h"

#include <algorithm>
#include <format>
#include <optional>
#include <utility>

namespace bitoscope::gui_decode {

namespace decode = bitoscope::decode;

namespace {

bool contains(const std::vector<std::string>& names, std::string_view name) {
  return std::find(names.begin(), names.end(), name) != names.end();
}

decode::ChannelMap boundPositions(const DecoderConfig& config, const decode::Decoder& decoder,
                                  std::span<const CapturedChannel> captured) {
  decode::ChannelMap out;
  for (const decode::ChannelSpec& spec : decoder.channels()) {
    const std::optional<std::uint32_t> device = config.channelBinding(spec.name);
    if (!device) continue;
    for (std::size_t i = 0; i < captured.size(); ++i) {
      if (captured[i].deviceChannel != *device) continue;
      out[spec.name] = static_cast<std::uint32_t>(i);
      break;
    }
  }
  return out;
}

decode::OptionMap resolvedOptions(const DecoderConfig& config, const decode::Decoder& decoder) {
  decode::OptionMap out;
  for (const decode::OptionSpec& spec : decoder.options()) out[spec.name] = config.optionValue(spec.name, spec);
  return out;
}

DerivationContext contextOf(const decode::Decoder& decoder, decode::OptionMap options, decode::ChannelMap channels,
                            std::uint64_t sampleRateMilliHz) {
  DerivationContext context;
  context.rowSources = decoder.rowSources();
  context.options = std::move(options);
  context.channels = std::move(channels);
  context.sampleRateMilliHz = sampleRateMilliHz;
  for (const decode::RowSource& source : context.rowSources) {
    if (!decode::derivesAnnotations(source)) continue;
    DerivedRow row{source, decode::resolveRowRule(source, context.options, context.channels,
                                                  context.sampleRateMilliHz)};
    if (const decode::RowSource* other = decode::notDerivedSharing(context.rowSources, source.enable_option);
        other != nullptr && !source.enable_option.empty()) {
      row.rule = std::unexpected(std::format(
          "option '{}' also governs row {}, whose rule this build does not know: decoder emits row itself",
          source.enable_option, other->row));
    }
    context.derived.push_back(std::move(row));
  }
  return context;
}

}

std::vector<std::string> derivedRowEnableOptions(const decode::Decoder& decoder) {
  return decode::derivedRowEnableOptions(decoder.rowSources());
}

bool isDerivedRowEnableOption(const decode::DecoderRegistry& registry, std::string_view decoderName,
                              std::string_view optionName) {
  auto decoder = registry.create(std::string(decoderName));
  if (!decoder) return false;
  return contains(derivedRowEnableOptions(**decoder), optionName);
}

decode::OptionMap runOptions(const DecoderConfig& config, const decode::Decoder& decoder) {
  return decode::withDerivedRowsOff(decoder.rowSources(), resolvedOptions(config, decoder));
}

const DerivedRow* derivedRow(const DerivationContext& context, std::uint32_t row) {
  for (const DerivedRow& d : context.derived) {
    if (d.source.row == row) return &d;
  }
  return nullptr;
}

DerivationContext runDerivationContext(const DecoderConfig& config, const decode::DecoderRegistry& registry,
                                       std::span<const CapturedChannel> captured, std::uint64_t sampleRateMilliHz) {
  auto decoder = registry.create(config.decoderName());
  if (!decoder) return {};
  return contextOf(**decoder, runOptions(config, **decoder), boundPositions(config, **decoder, captured),
                   sampleRateMilliHz);
}

}
