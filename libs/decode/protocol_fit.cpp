/*
 * SPDX-License-Identifier: MIT
 */

#include "protocol_fit.h"

#include <algorithm>
#include <format>
#include <optional>
#include <utility>
#include <vector>

#include "decoder.h"
#include "display_text.h"
#include "spec_display.h"

namespace bitoscope::decode {

bool protocolAllows(const Decoder& reader, std::string_view carried) {
  const std::vector<std::string> wanted = reader.protocols();
  if (wanted.empty()) return true;
  return std::find(wanted.begin(), wanted.end(), carried) != wanted.end();
}

std::optional<std::string> resolveOutputCarries(const Decoder& producer, const OutputSpec& output) {
  const Decoder* at = &producer;
  const OutputSpec* spec = &output;
  std::vector<std::pair<const Decoder*, std::string>> followed;
  for (;;) {
    if (spec->carries) return *spec->carries;
    if (!spec->carries_input) return std::string{};
    if (at == nullptr) return std::string{};
    // WHY: An unconfigured producer yields nullopt, not empty text: what its pass-through carries is not yet knowable.
    if (!at->isConfigured()) return std::nullopt;
    const std::pair<const Decoder*, std::string> step{at, *spec->carries_input};
    if (std::find(followed.begin(), followed.end(), step) != followed.end()) return std::string{};
    followed.push_back(step);
    const InputMap& bound = at->boundInputs();
    const auto it = bound.find(*spec->carries_input);
    if (it == bound.end()) return std::string{};
    at = it->second.from;
    spec = &it->second.output;
  }
}

std::string outputCarries(const Decoder& producer, const OutputSpec& output) {
  return resolveOutputCarries(producer, output).value_or(std::string{});
}

std::expected<void, std::string> checkProtocolFit(const Decoder& reader, std::string_view binding,
                                                  const Decoder& producer, const OutputSpec& output) {
  const std::string carried = outputCarries(producer, output);
  if (protocolAllows(reader, carried)) return {};
  std::string designed;
  for (const std::string& id : reader.protocols()) {
    if (!designed.empty()) designed += ", ";
    designed += display_text::displayName(id);
  }
  const std::string carries = carried.empty() ? std::string("nothing") : display_text::displayName(carried);
  return std::unexpected(
      std::format("{} is designed for {}; {} carries {}", displayOf(reader), designed, binding, carries));
}

}
