/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <expected>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "decoder.h"

namespace bitoscope::waveform_view {

using RowClass = std::pair<std::uint32_t, std::uint32_t>;

struct DecoderVisibility {
  std::set<std::uint32_t> hiddenRows;
  std::set<RowClass> hiddenClasses;
  bool empty() const { return hiddenRows.empty() && hiddenClasses.empty(); }
  bool operator==(const DecoderVisibility&) const = default;
};

bool rowHidden(const DecoderVisibility& visibility, std::uint32_t row);
bool classHidden(const DecoderVisibility& visibility, std::uint32_t row, std::uint32_t cls);

bool setRowHidden(DecoderVisibility& visibility, std::uint32_t row, bool hidden);
bool setClassHidden(DecoderVisibility& visibility, std::uint32_t row, std::uint32_t cls, bool hidden);
bool setClassesHidden(DecoderVisibility& visibility, std::uint32_t row, std::span<const std::uint32_t> classes,
                      bool hidden);

std::optional<std::string> decoderVisibilityText(std::span<const std::string> keys,
                                                 std::span<const DecoderVisibility> visibilities);

struct SavedVisibility {
  DecoderVisibility visibility;
  std::set<std::uint32_t> classesInEveryRow;
  bool operator==(const SavedVisibility&) const = default;
};

std::expected<std::map<std::string, SavedVisibility, std::less<>>, std::string> parseDecoderVisibility(
    std::string_view text);

DecoderVisibility declaredVisibility(const SavedVisibility& saved, std::span<const decode::AnnotationRow> rows,
                                     std::span<const decode::AnnotationClass> classes);

}
