/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "decoder.h"

namespace bitoscope::gui_decode {

enum class WidgetKind { Dropdown, NumberBox, Slider, Text, Checkbox, Radio };

struct OptionWidget {
  WidgetKind kind = WidgetKind::Text;
  std::vector<std::string> choices;
  std::optional<int64_t> min;
  std::optional<int64_t> max;
  bool operator==(const OptionWidget&) const = default;
};

inline constexpr std::size_t kSliderMaxSteps = 8;

inline constexpr std::size_t kRadioMaxChoices = 4;

OptionWidget widgetFor(const bitoscope::decode::OptionSpec& spec);

std::optional<std::size_t> choiceIndex(const OptionWidget& widget, std::string_view text);

std::string optionValueText(const bitoscope::decode::OptionSpec& spec, std::string_view value);

std::optional<bool> checkboxState(std::string_view text);
std::string checkboxText(bool checked);

std::expected<std::string, std::string> clampedIntegerText(const bitoscope::decode::OptionSpec& spec,
                                                           std::string_view text);

struct FieldLine {
  std::string label;
  bool bold = false;
  std::string tooltip;
};

FieldLine channelLine(const bitoscope::decode::ChannelSpec& spec);
FieldLine optionLine(const bitoscope::decode::OptionSpec& spec);

}
