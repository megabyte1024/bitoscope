/*
 * SPDX-License-Identifier: MIT
 */

#include "option_widget.h"

#include <algorithm>
#include <cctype>
#include <limits>

#include "decoder_labels.h"
#include "display_text.h"
#include "option_text.h"

namespace bitoscope::gui_decode {

namespace decode = bitoscope::decode;
namespace display_text = bitoscope::display_text;

namespace {

bool isDigits(std::string_view s) {
  return !s.empty() && std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isdigit(c) != 0; });
}

bool isNumeric(std::string_view s) {
  if (!s.empty() && (s.front() == '+' || s.front() == '-')) s.remove_prefix(1);
  const std::size_t dot = s.find('.');
  if (dot == std::string_view::npos) return isDigits(s);
  return isDigits(s.substr(0, dot)) && isDigits(s.substr(dot + 1));
}

std::string_view trimmed(std::string_view s) {
  while (!s.empty() && s.front() == ' ') s.remove_prefix(1);
  while (!s.empty() && s.back() == ' ') s.remove_suffix(1);
  return s;
}

}

OptionWidget widgetFor(const decode::OptionSpec& spec) {
  OptionWidget widget;
  for (const std::string& value : spec.allowed_values) {
    if (std::find(spec.unsupported_values.begin(), spec.unsupported_values.end(), value) ==
            spec.unsupported_values.end()) {
      widget.choices.push_back(value);
    }
  }
  if (!widget.choices.empty()) {
    const auto offered = [&](std::string_view v) {
      return std::find(widget.choices.begin(), widget.choices.end(), v) != widget.choices.end();
    };
    if (spec.type == decode::OptionType::String && widget.choices.size() == 2 && offered("yes") && offered("no")) {
      widget.kind = WidgetKind::Checkbox;
      return widget;
    }
    // WHY: A String option's numbers are names, not slider steps (UART stop bits 1, 1.5, 2).
    const bool steps = spec.type == decode::OptionType::Integer && widget.choices.size() >= 2 &&
                       widget.choices.size() <= kSliderMaxSteps &&
                       std::all_of(widget.choices.begin(), widget.choices.end(), isNumeric);
    if (steps) {
      widget.kind = WidgetKind::Slider;
    } else {
      widget.kind = widget.choices.size() <= kRadioMaxChoices && widget.choices.size() >= 2 ? WidgetKind::Radio
                                                                                            : WidgetKind::Dropdown;
    }
    return widget;
  }
  if (!spec.allowed_values.empty()) return widget;
  if (spec.type == decode::OptionType::Integer && spec.min && spec.max && *spec.min <= *spec.max) {
    widget.min = spec.min;
    widget.max = spec.max;
    const uint64_t span = static_cast<uint64_t>(*spec.max) - static_cast<uint64_t>(*spec.min);
    widget.kind = span < kSliderMaxSteps ? WidgetKind::Slider : WidgetKind::NumberBox;
  }
  return widget;
}

std::optional<std::size_t> choiceIndex(const OptionWidget& widget, std::string_view text) {
  auto it = std::find(widget.choices.begin(), widget.choices.end(), text);
  if (it == widget.choices.end()) {
    it = std::find(widget.choices.begin(), widget.choices.end(), decode::canonicalOptionText(text));
  }
  if (it == widget.choices.end()) return std::nullopt;
  return static_cast<std::size_t>(it - widget.choices.begin());
}

std::string optionValueText(const decode::OptionSpec&, std::string_view value) {
  std::string shown = display_text::displayText(value);
  if (shown == value) return shown;
  std::replace(shown.begin(), shown.end(), '-', ' ');
  return shown;
}

std::optional<bool> checkboxState(std::string_view text) {
  if (text == "yes") return true;
  if (text == "no") return false;
  return std::nullopt;
}

std::string checkboxText(bool checked) { return checked ? "yes" : "no"; }

std::expected<std::string, std::string> clampedIntegerText(const decode::OptionSpec& spec, std::string_view text) {
  std::string_view s = trimmed(text);
  bool negative = false;
  if (!s.empty() && (s.front() == '+' || s.front() == '-')) {
    negative = s.front() == '-';
    s.remove_prefix(1);
  }
  if (!isDigits(s)) return std::unexpected("whole number expected");

  constexpr int64_t kMax = std::numeric_limits<int64_t>::max();
  constexpr int64_t kMin = std::numeric_limits<int64_t>::min();
  int64_t value = 0;
  bool saturated = false;
  for (const char c : s) {
    const int digit = c - '0';
    if (value > (kMax - digit) / 10) {
      saturated = true;
      break;
    }
    value = value * 10 + digit;
  }
  if (saturated) value = negative ? kMin : kMax;
  else if (negative) value = -value;

  if (spec.min && spec.max && *spec.min > *spec.max) return std::to_string(value);
  if (spec.min && value < *spec.min) value = *spec.min;
  if (spec.max && value > *spec.max) value = *spec.max;
  return std::to_string(value);
}

FieldLine channelLine(const decode::ChannelSpec& spec) {
  return {humanLabel(spec.name, spec.display), spec.required, channelTooltip(spec)};
}

FieldLine optionLine(const decode::OptionSpec& spec) {
  return {humanLabel(spec.name, spec.display), false, optionTooltip(spec)};
}

}
