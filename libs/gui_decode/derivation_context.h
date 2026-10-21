/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "decoder.h"
#include "decoder_config.h"
#include "decoder_registry.h"
#include "derived_row_run.h"
#include "derived_rows.h"
#include "row_source.h"

namespace bitoscope::gui_decode {

std::vector<std::string> derivedRowEnableOptions(const bitoscope::decode::Decoder& decoder);

bool isDerivedRowEnableOption(const bitoscope::decode::DecoderRegistry& registry, std::string_view decoderName,
                              std::string_view optionName);

bitoscope::decode::OptionMap runOptions(const DecoderConfig& config, const bitoscope::decode::Decoder& decoder);

using ResolvedRule = bitoscope::decode::ResolvedRule;

struct DerivedRow {
  bitoscope::decode::RowSource source;
  std::expected<ResolvedRule, std::string> rule;
};

struct DerivationContext {
  std::vector<bitoscope::decode::RowSource> rowSources;
  bitoscope::decode::OptionMap options;
  bitoscope::decode::ChannelMap channels;
  std::uint64_t sampleRateMilliHz = 0;
  std::vector<DerivedRow> derived;
};

const DerivedRow* derivedRow(const DerivationContext& context, std::uint32_t row);

DerivationContext runDerivationContext(const DecoderConfig& config, const bitoscope::decode::DecoderRegistry& registry,
                                       std::span<const CapturedChannel> captured, std::uint64_t sampleRateMilliHz);

}
