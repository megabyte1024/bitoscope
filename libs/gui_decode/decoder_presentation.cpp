/*
 * SPDX-License-Identifier: MIT
 */

#include "decoder_presentation.h"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <map>
#include <optional>
#include <set>

namespace bitoscope::gui_decode {

namespace decode = bitoscope::decode;

namespace {

constexpr std::string_view kColor = "color";
constexpr std::string_view kClassPrefix = "class.";
constexpr std::string_view kFormatPrefix = "format.";

std::optional<std::uint32_t> idOf(std::string_view digits) {
  std::uint32_t id = 0;
  const auto [end, ec] = std::from_chars(digits.data(), digits.data() + digits.size(), id);
  if (digits.empty() || ec != std::errc() || end != digits.data() + digits.size()) return std::nullopt;
  return id;
}

std::string_view dataFormatWord(decode::DataFormat format) {
  switch (format) {
  case decode::DataFormat::Dec:
    return "dec";
  case decode::DataFormat::Ascii:
    return "ascii";
  case decode::DataFormat::Hex:
    break;
  }
  return "hex";
}

std::optional<decode::DataFormat> dataFormatOf(std::string_view word) {
  if (word == "hex") return decode::DataFormat::Hex;
  if (word == "dec") return decode::DataFormat::Dec;
  if (word == "ascii") return decode::DataFormat::Ascii;
  return std::nullopt;
}

}

std::string presentationText(const DecoderConfig& config) {
  std::string out;
  const auto append = [&](std::string_view key, std::string_view value) {
    if (!out.empty()) out += ' ';
    out += key;
    out += '=';
    out += value;
  };
  if (config.color()) append(kColor, waveform_view::toText(*config.color()));
  for (const auto& [pair, color] : config.classColors()) {
    append(std::string(kClassPrefix) + std::to_string(pair.first) + "." + std::to_string(pair.second),
           waveform_view::toText(color));
  }
  for (const auto& [row, format] : config.rowDataFormats()) {
    append(std::string(kFormatPrefix) + std::to_string(row), dataFormatWord(format));
  }
  return out;
}

void applyPresentation(DecoderConfig& config, std::string_view text, const decode::DecoderRegistry& registry) {
  std::set<std::uint32_t> declaredClasses;
  std::vector<decode::AnnotationRow> rows;
  if (auto decoder = registry.create(config.decoderName())) {
    for (const decode::AnnotationClass& c : (*decoder)->annotationClasses()) declaredClasses.insert(c.id);
    rows = (*decoder)->rows();
  }
  const auto rowDeclared = [&](std::uint32_t row) {
    return rows.empty() || std::ranges::any_of(rows, [&](const decode::AnnotationRow& r) { return r.id == row; });
  };
  config.setColor(std::nullopt);
  for (const auto& [pair, color] : std::map(config.classColors())) {
    config.setClassColor(pair.first, pair.second, std::nullopt);
  }
  for (const auto& [row, format] : std::map(config.rowDataFormats())) {
    config.setRowDataFormat(row, decode::DataFormat::Hex);
  }

  std::map<std::uint32_t, waveform_view::ColorChoice> classInEveryRow;
  std::map<waveform_view::RowClass, waveform_view::ColorChoice> perRow;
  std::size_t at = 0;
  while (at < text.size()) {
    const std::size_t end = (std::min)(text.find(' ', at), text.size());
    const std::string_view token = text.substr(at, end - at);
    at = end + 1;
    const std::size_t eq = token.find('=');
    if (eq == std::string_view::npos) continue;
    const std::string_view key = token.substr(0, eq);
    const std::string_view value = token.substr(eq + 1);
    if (key == kColor) {
      if (auto color = waveform_view::fromText(value)) config.setColor(*color);
    } else if (key.starts_with(kClassPrefix)) {
      const std::string_view ids = key.substr(kClassPrefix.size());
      const std::size_t dot = ids.find('.');
      const auto color = waveform_view::fromText(value);
      if (!color) continue;
      if (dot == std::string_view::npos) {
        const auto cls = idOf(ids);
        if (cls && declaredClasses.contains(*cls)) classInEveryRow[*cls] = *color;
        continue;
      }
      const auto row = idOf(ids.substr(0, dot));
      const auto cls = idOf(ids.substr(dot + 1));
      if (row && cls && rowDeclared(*row) && declaredClasses.contains(*cls)) perRow[{*row, *cls}] = *color;
    } else if (key.starts_with(kFormatPrefix)) {
      const auto row = idOf(key.substr(kFormatPrefix.size()));
      const auto format = dataFormatOf(value);
      if (row && format && rowDeclared(*row)) config.setRowDataFormat(*row, *format);
    }
  }
  // WHY: Apply a class-only word to every declared row: it was written when one colour held in every row it occurs in.
  for (const auto& [cls, color] : classInEveryRow) {
    for (const decode::AnnotationRow& row : rows) config.setClassColor(row.id, cls, color);
  }
  for (const auto& [pair, color] : perRow) config.setClassColor(pair.first, pair.second, color);
}

std::vector<std::string> decoderPresentationOf(std::span<const DecoderConfig> configs,
                                               const decode::DecoderRegistry& registry) {
  std::vector<std::string> out;
  for (const DecoderConfig& config : configs) {
    if (registry.create(config.decoderName())) out.push_back(presentationText(config));
  }
  return out;
}

void applyLoadedPresentations(std::span<DecoderConfig> configs, std::span<const std::optional<std::size_t>> entries,
                              std::span<const std::string> presentation, const decode::DecoderRegistry& registry) {
  for (std::size_t k = 0; k < configs.size() && k < entries.size(); ++k) {
    if (!entries[k] || *entries[k] >= presentation.size()) continue;
    applyPresentation(configs[k], presentation[*entries[k]], registry);
  }
}

}
