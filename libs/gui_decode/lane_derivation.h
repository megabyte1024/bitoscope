/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "annotation_store.h"
#include "decoder.h"
#include "decoder_visibility.h"
#include "derived_lane_rows.h"

namespace bitoscope::gui_decode {

struct LaneDerivation {
  bitoscope::waveform_view::DerivedRows rows;
  std::vector<std::pair<std::uint32_t, std::string>> notes;
  std::vector<bitoscope::waveform_view::RowClass> switchedOff;
};

LaneDerivation laneDerivation(const ShownResult& shown, const bitoscope::decode::SignalView* capture,
                              bool showDerived = true);

bitoscope::waveform_view::DecoderVisibility laneVisibility(const LaneDerivation& lane);

const std::string* derivationNote(const LaneDerivation& lane, std::uint32_t row);

}
