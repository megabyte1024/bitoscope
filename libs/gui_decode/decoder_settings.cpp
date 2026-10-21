/*
 * SPDX-License-Identifier: MIT
 */

#include "decoder_settings.h"

#include <algorithm>
#include <charconv>
#include <limits>

#include "decoder_chain_config.h"
#include "decoder_labels.h"

namespace bitoscope::gui_decode {

namespace decode = bitoscope::decode;

namespace {

std::string_view withoutSpaces(std::string_view s) {
  while (!s.empty() && s.front() == ' ') s.remove_prefix(1);
  while (!s.empty() && s.back() == ' ') s.remove_suffix(1);
  return s;
}

std::optional<std::int64_t> wholeNumber(std::string_view text) {
  std::int64_t value = 0;
  const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
  if (ec != std::errc() || end != text.data() + text.size() || text.empty()) return std::nullopt;
  return value;
}

}

bool anyRequiredChannel(std::span<const decode::ChannelSpec> channels) {
  return std::ranges::any_of(channels, [](const decode::ChannelSpec& c) { return c.required; });
}

std::expected<bool, std::string> commitDecoderName(std::span<DecoderConfig> configs, std::size_t index,
                                                   std::string_view typed, const decode::DecoderRegistry& registry) {
  const std::string_view name = withoutSpaces(typed);
  if (index >= configs.size()) {
    if (auto renamed = renameDecoder(configs, index, name, registry); !renamed) return std::unexpected(renamed.error());
    return true;
  }
  const DecoderConfig& config = configs[index];
  if (name == config.id() || name == displayInstanceName(config.id(), config.decoderName()) ||
      name == decoderBarName(configs, index, registry)) {
    return false;
  }
  if (name == decoderListName(configs, index, registry)) {
    if (auto renamed = renameDecoder(configs, index, uniqueDecoderId(config.decoderName(), configs), registry); !renamed) {
      return std::unexpected(renamed.error());
    }
    return true;
  }
  const std::string stored = storedInstanceId(name, config.decoderName());
  if (auto renamed = renameDecoder(configs, index, stored, registry); !renamed) {
    std::string message = renamed.error();
    const std::string asked = "'" + stored + "'";
    if (const std::size_t at = message.find(asked); at != std::string::npos && stored != name) {
      message.replace(at, asked.size(), "'" + std::string(name) + "'");
    }
    return std::unexpected(std::move(message));
  }
  return true;
}

std::string steppedIntegerText(const decode::OptionSpec& spec, std::string_view current, std::int64_t delta) {
  std::expected<std::string, std::string> start = clampedIntegerText(spec, current);
  if (!start) start = clampedIntegerText(spec, spec.default_value);
  if (!start) start = clampedIntegerText(spec, spec.min ? std::to_string(*spec.min) : std::string("0"));
  std::int64_t value = start ? wholeNumber(*start).value_or(0) : 0;
  constexpr std::int64_t kMax = std::numeric_limits<std::int64_t>::max();
  constexpr std::int64_t kMin = std::numeric_limits<std::int64_t>::min();
  if (delta > 0 && value > kMax - delta) {
    value = kMax;
  } else if (delta < 0 && value < kMin - delta) {
    value = kMin;
  } else {
    value += delta;
  }
  return clampedIntegerText(spec, std::to_string(value)).value_or(std::to_string(value));
}

std::size_t sliderStepCount(const OptionWidget& widget) {
  if (!widget.choices.empty()) return widget.choices.size();
  if (widget.min && widget.max && *widget.min <= *widget.max) {
    return static_cast<std::size_t>(static_cast<std::uint64_t>(*widget.max) - static_cast<std::uint64_t>(*widget.min)) + 1;
  }
  return 0;
}

std::optional<std::size_t> sliderPosition(const OptionWidget& widget, std::string_view text) {
  if (!widget.choices.empty()) return choiceIndex(widget, text);
  if (sliderStepCount(widget) == 0) return std::nullopt;
  const std::optional<std::int64_t> value = wholeNumber(text);
  if (!value || *value < *widget.min || *value > *widget.max) return std::nullopt;
  return static_cast<std::size_t>(static_cast<std::uint64_t>(*value) - static_cast<std::uint64_t>(*widget.min));
}

std::string sliderText(const OptionWidget& widget, std::size_t position) {
  const std::size_t steps = sliderStepCount(widget);
  if (steps == 0) return {};
  position = (std::min)(position, steps - 1);
  if (!widget.choices.empty()) return widget.choices[position];
  return std::to_string(static_cast<std::int64_t>(static_cast<std::uint64_t>(*widget.min) + position));
}

}
