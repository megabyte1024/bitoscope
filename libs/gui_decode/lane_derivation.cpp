/*
 * SPDX-License-Identifier: MIT
 */

#include "lane_derivation.h"

#include <algorithm>
#include <variant>

namespace bitoscope::gui_decode {

namespace decode = bitoscope::decode;
namespace wv = bitoscope::waveform_view;

LaneDerivation laneDerivation(const ShownResult& shown, const decode::SignalView* capture, bool showDerived) {
  LaneDerivation out;
  if (!showDerived) {
    for (const DerivedRow& row : shown.derivation.derived) {
      out.switchedOff.emplace_back(row.source.row, row.source.cls);
      if (shown.annotations == nullptr) continue;
      for (const std::uint32_t cls : shown.annotations->groups().rowClasses(row.source.row)) {
        if (cls != row.source.cls) out.switchedOff.emplace_back(row.source.row, cls);
      }
    }
    return out;
  }
  if (shown.annotations == nullptr) return out;
  const wv::AnnotationGroups& groups = shown.annotations->groups();
  for (const DerivedRow& row : shown.derivation.derived) {
    if (!row.rule) {
      out.notes.emplace_back(row.source.row, row.rule.error());
      continue;
    }
    wv::DerivedRowSource source;
    source.row = row.source.row;
    source.cls = row.source.cls;
    for (const std::uint32_t cls : row.source.source_classes) {
      if (const auto g = groups.findGroup(row.source.source_row, cls)) source.sourceGroups.push_back(*g);
    }
    std::ranges::sort(source.sourceGroups);
    if (const auto* grid = std::get_if<decode::BitGrid>(&*row.rule)) {
      source.rule = *grid;
    } else {
      if (capture == nullptr) {
        out.notes.emplace_back(row.source.row, "no capture to read clock edges from");
        continue;
      }
      if (std::string failed = capture->readError(); !failed.empty()) {
        out.notes.emplace_back(row.source.row, std::move(failed));
        continue;
      }
      source.rule = wv::EdgeRule{std::get<decode::ClockEdges>(*row.rule), capture};
    }
    out.rows.rows.push_back(std::move(source));
  }
  return out;
}

wv::DecoderVisibility laneVisibility(const wv::DecoderVisibility& user, const LaneDerivation& lane) {
  wv::DecoderVisibility out = user;
  for (const wv::RowClass& rowClass : lane.switchedOff) out.hiddenClasses.insert(rowClass);
  return out;
}

const std::string* derivationNote(const LaneDerivation& lane, std::uint32_t row) {
  for (const auto& [id, note] : lane.notes) {
    if (id == row) return &note;
  }
  return nullptr;
}

}
