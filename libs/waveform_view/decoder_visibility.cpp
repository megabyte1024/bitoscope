/*
 * SPDX-License-Identifier: MIT
 */

#include "decoder_visibility.h"

#include <algorithm>
#include <charconv>
#include <format>
#include <system_error>

namespace bitoscope::waveform_view {

namespace {

template <class Key>
bool setHidden(std::set<Key>& hidden, const Key& key, bool hide) {
  if (hide) return hidden.insert(key).second;
  return hidden.erase(key) > 0;
}

bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

std::optional<std::uint32_t> decimalId(std::string_view digits) {
  std::uint32_t id = 0;
  if (digits.empty() || !(digits.front() >= '0' && digits.front() <= '9')) return std::nullopt;
  const auto [ptr, ec] = std::from_chars(digits.data(), digits.data() + digits.size(), id);
  if (ec != std::errc{} || ptr != digits.data() + digits.size()) return std::nullopt;
  return id;
}

}

bool rowHidden(const DecoderVisibility& visibility, std::uint32_t row) { return visibility.hiddenRows.contains(row); }

bool classHidden(const DecoderVisibility& visibility, std::uint32_t row, std::uint32_t cls) {
  return visibility.hiddenClasses.contains({row, cls});
}

bool setRowHidden(DecoderVisibility& visibility, std::uint32_t row, bool hidden) {
  return setHidden(visibility.hiddenRows, row, hidden);
}

bool setClassHidden(DecoderVisibility& visibility, std::uint32_t row, std::uint32_t cls, bool hidden) {
  return setHidden(visibility.hiddenClasses, RowClass{row, cls}, hidden);
}

bool setClassesHidden(DecoderVisibility& visibility, std::uint32_t row, std::span<const std::uint32_t> classes,
                      bool hidden) {
  bool changed = false;
  for (const std::uint32_t cls : classes) changed = setClassHidden(visibility, row, cls, hidden) || changed;
  return changed;
}

std::optional<std::string> decoderVisibilityText(std::span<const std::string> keys,
                                                 std::span<const DecoderVisibility> visibilities) {
  std::string text;
  const std::size_t count = (std::min)(keys.size(), visibilities.size());
  for (std::size_t d = 0; d < count; ++d) {
    const DecoderVisibility& v = visibilities[d];
    if (v.empty()) continue;
    if (!text.empty()) text += ' ';
    text += std::format("[{}]", keys[d]);
    bool first = true;
    for (const std::uint32_t row : v.hiddenRows) {
      text += std::format("{}r{}", first ? "" : ",", row);
      first = false;
    }
    for (const auto& [row, cls] : v.hiddenClasses) {
      text += std::format("{}c{}.{}", first ? "" : ",", row, cls);
      first = false;
    }
  }
  if (text.empty()) return std::nullopt;
  return text;
}

std::expected<std::map<std::string, SavedVisibility, std::less<>>, std::string> parseDecoderVisibility(
    std::string_view text) {
  std::map<std::string, SavedVisibility, std::less<>> out;
  std::size_t at = 0;
  while (true) {
    while (at < text.size() && isSpace(text[at])) ++at;
    if (at >= text.size()) break;
    std::size_t end = at;
    while (end < text.size() && !isSpace(text[end])) ++end;
    const std::string_view token = text.substr(at, end - at);
    at = end;
    const auto refuse = [&](std::string_view why) {
      return std::unexpected(std::format("hidden rows and classes '{}': {}", token, why));
    };
    if (token.front() != '[') return refuse("decoder key in square brackets expected");
    const std::size_t close = token.find(']');
    if (close == std::string_view::npos) return refuse("decoder key has no closing bracket");
    const std::string_view key = token.substr(1, close - 1);
    if (key.empty()) return refuse("decoder key is empty");
    if (out.contains(key)) return refuse("decoder named twice");
    SavedVisibility saved;
    std::string_view items = token.substr(close + 1);
    while (!items.empty()) {
      const std::size_t comma = items.find(',');
      const std::string_view item = items.substr(0, comma);
      items = comma == std::string_view::npos ? std::string_view{} : items.substr(comma + 1);
      if (item.empty() || (comma != std::string_view::npos && items.empty())) return refuse("empty item");
      const char letter = item.front();
      if (letter != 'r' && letter != 'c') continue;
      const std::string_view body = item.substr(1);
      const std::size_t dot = body.find('.');
      const auto bad = [&] { return refuse(std::format("item '{}' has no valid id", item)); };
      if (letter == 'r') {
        const auto row = decimalId(body);
        if (!row) return bad();
        saved.visibility.hiddenRows.insert(*row);
      } else if (dot == std::string_view::npos) {
        const auto cls = decimalId(body);
        if (!cls) return bad();
        saved.classesInEveryRow.insert(*cls);
      } else {
        const auto row = decimalId(body.substr(0, dot));
        const auto cls = decimalId(body.substr(dot + 1));
        if (!row || !cls) return bad();
        saved.visibility.hiddenClasses.insert({*row, *cls});
      }
    }
    out.emplace(std::string(key), std::move(saved));
  }
  return out;
}

DecoderVisibility declaredVisibility(const SavedVisibility& saved, std::span<const decode::AnnotationRow> rows,
                                     std::span<const decode::AnnotationClass> classes) {
  const auto rowDeclared = [&](std::uint32_t row) {
    return rows.empty() || std::ranges::any_of(rows, [&](const decode::AnnotationRow& r) { return r.id == row; });
  };
  const auto classDeclared = [&](std::uint32_t cls) {
    return std::ranges::any_of(classes, [&](const decode::AnnotationClass& c) { return c.id == cls; });
  };
  DecoderVisibility kept;
  for (const std::uint32_t row : saved.visibility.hiddenRows) {
    if (rowDeclared(row)) kept.hiddenRows.insert(row);
  }
  for (const RowClass& pair : saved.visibility.hiddenClasses) {
    if (rowDeclared(pair.first) && classDeclared(pair.second)) kept.hiddenClasses.insert(pair);
  }
  for (const std::uint32_t cls : saved.classesInEveryRow) {
    if (!classDeclared(cls)) continue;
    for (const decode::AnnotationRow& row : rows) kept.hiddenClasses.insert({row.id, cls});
  }
  return kept;
}

}
