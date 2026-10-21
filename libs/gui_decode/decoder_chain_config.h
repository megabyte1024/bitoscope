/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "decoder.h"
#include "decoder_chain.h"
#include "decoder_config.h"
#include "decoder_registry.h"

namespace bitoscope::gui_decode {

struct ValidatedChain {
  bitoscope::decode::ChainPlan plan;
  std::vector<std::size_t> configIndices;
};

ValidatedChain validateAll(std::span<DecoderConfig> configs, const bitoscope::decode::DecoderRegistry& registry,
                           std::span<const CapturedChannel> captured, const bitoscope::decode::CaptureFacts& facts);

ValidatedChain validateForRun(std::span<DecoderConfig> configs, const bitoscope::decode::DecoderRegistry& registry,
                              std::span<const CapturedChannel> captured, const bitoscope::decode::CaptureFacts& facts);

struct BindingCandidate {
  std::size_t config = 0;
  std::string id;
  std::string output;
  std::string binding;
  std::optional<std::string> carries;
  std::optional<std::string> notDesignedFor;
  bool operator==(const BindingCandidate&) const = default;
};

enum class Availability { ChannelsBound, Declared };
std::vector<BindingCandidate> bindingCandidates(std::span<const DecoderConfig> configs, std::size_t index,
                                                std::string_view inputName,
                                                const bitoscope::decode::DecoderRegistry& registry,
                                                Availability availability = Availability::ChannelsBound);

std::vector<BindingCandidate> dropdownCandidates(std::span<const DecoderConfig> configs, std::size_t index,
                                                 std::string_view inputName,
                                                 const bitoscope::decode::DecoderRegistry& registry);

std::vector<std::size_t> laneOrder(std::span<const DecoderConfig> configs);

struct DecoderReads {
  std::vector<std::uint32_t> channels;
  std::vector<std::size_t> decoders;
  bool operator==(const DecoderReads&) const = default;
};
DecoderReads decoderReads(std::span<const DecoderConfig> configs, std::size_t index);

std::expected<void, std::string> renameDecoder(std::span<DecoderConfig> configs, std::size_t index,
                                               std::string_view newId,
                                               const bitoscope::decode::DecoderRegistry& registry);

std::vector<std::pair<std::string, std::string>> decoderSetupOf(std::span<const DecoderConfig> configs,
                                                                const bitoscope::decode::DecoderRegistry& registry);

}
