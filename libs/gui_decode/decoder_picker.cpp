/*
 * SPDX-License-Identifier: MIT
 */

#include "decoder_picker.h"

#include <algorithm>

#include "decoder.h"
#include "decoder_labels.h"
#include "decoder_versions.h"

namespace bitoscope::gui_decode {

namespace decode = bitoscope::decode;

namespace {

// WHY: Lower-case ASCII only: bytes >= 0x80 (superscript digits in shown names) must stay as they are.
char lowerAscii(char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }

bool containsNoCase(std::string_view text, std::string_view part) {
  return std::search(text.begin(), text.end(), part.begin(), part.end(),
                     [](char a, char b) { return lowerAscii(a) == lowerAscii(b); }) != text.end();
}

}

std::vector<PickerEntry> decoderPickerEntries(const decode::DecoderRegistry& registry) {
  std::vector<PickerEntry> protocols;
  std::vector<PickerEntry> streams;
  for (const std::string& name : registry.names()) {
    const auto decoder = registry.create(name);
    if (!decoder) continue;
    const decode::Reads reads = (*decoder)->reads();
    std::string display = decoderDisplay(registry, name);
    const DecoderVersions versions = decoderVersionsOf(registry, name);
    if (reads != decode::Reads::Streams) {
      protocols.push_back({name, display, PickerGroup::Protocol, versions.decoderVersion, versions.plugin,
                           versions.pluginVersion});
    }
    if (reads != decode::Reads::Samples) {
      streams.push_back({name, std::move(display), PickerGroup::Stream, versions.decoderVersion, versions.plugin,
                         versions.pluginVersion});
    }
  }
  protocols.insert(protocols.end(), std::make_move_iterator(streams.begin()), std::make_move_iterator(streams.end()));
  return protocols;
}

bool pickerEntryMatches(const PickerEntry& entry, std::string_view filter) {
  return containsNoCase(entry.id, filter) || containsNoCase(entry.display, filter);
}

std::vector<PickerRow> pickerRows(const std::vector<PickerEntry>& entries, std::string_view filter) {
  std::vector<PickerRow> rows;
  bool protocolSeen = false;
  bool separatorPending = false;
  for (const PickerEntry& entry : entries) {
    if (!pickerEntryMatches(entry, filter)) continue;
    if (entry.group == PickerGroup::Protocol) {
      protocolSeen = true;
    } else if (protocolSeen && !separatorPending) {
      rows.push_back({true, {}});
      separatorPending = true;
    }
    rows.push_back({false, entry});
  }
  return rows;
}

float clampPickerHeight(float available, float requested, float rowHeight) {
  const float minimum = static_cast<float>(kPickerMinRows) * rowHeight;
  return std::max(minimum, std::min(requested, available));
}

}
