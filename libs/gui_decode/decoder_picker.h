/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "decoder_registry.h"

namespace bitoscope::gui_decode {

enum class PickerGroup { Protocol, Stream };

struct PickerEntry {
  std::string id;
  std::string display;
  PickerGroup group = PickerGroup::Protocol;
  std::string version{};
  std::string plugin{};
  std::string pluginVersion{};
  bool operator==(const PickerEntry&) const = default;
};

struct PickerRow {
  bool separator = false;
  PickerEntry entry;
  bool operator==(const PickerRow&) const = default;
};

inline constexpr int kPickerMinRows = 5;

std::vector<PickerEntry> decoderPickerEntries(const bitoscope::decode::DecoderRegistry& registry);

bool pickerEntryMatches(const PickerEntry& entry, std::string_view filter);

std::vector<PickerRow> pickerRows(const std::vector<PickerEntry>& entries, std::string_view filter);

float clampPickerHeight(float available, float requested, float rowHeight);

}
