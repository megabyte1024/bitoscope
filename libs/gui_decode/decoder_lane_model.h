/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "annotation_store.h"
#include "decoder.h"
#include "decoder_config.h"
#include "decoder_registry.h"
#include "annotation_row_layout.h"
#include "decoder_visibility.h"
#include "decoding_prefix.h"
#include "lane_derivation.h"
#include "row_source.h"

namespace bitoscope::gui_decode {

struct DecoderLaneLabels {
  std::vector<bitoscope::decode::AnnotationRow> declared;
  std::vector<bitoscope::decode::RowSource> rowSources;
  std::string laneLabel;
  std::string barName;
  bool operator==(const DecoderLaneLabels&) const = default;
};

class DecoderLaneLabelCache {
 public:
  void refresh(std::span<const DecoderConfig> configs, const bitoscope::decode::DecoderRegistry& registry);
  const DecoderLaneLabels& at(std::size_t index) const { return labels_[index]; }
  std::size_t builds() const { return builds_; }

 private:
  struct ConfigKey {
    std::string decoderName;
    std::string id;
    std::map<std::string, std::string, std::less<>> inputBindings;
    bool operator==(const ConfigKey&) const = default;
  };
  std::vector<ConfigKey> configs_;
  const bitoscope::decode::DecoderRegistry* registry_ = nullptr;
  std::vector<std::string> registryNames_;
  std::vector<DecoderLaneLabels> labels_;
  std::size_t builds_ = 0;
};

struct DecoderLaneModelInput {
  const AnnotationStore* store = nullptr;
  std::size_t index = 0;
  const DecoderLaneLabels* labels = nullptr;
  const bitoscope::decode::SignalView* capture = nullptr;
  bool showDerived = true;
  double rowPitch = 0.0;
};

struct DecoderLaneModel {
  std::vector<bitoscope::decode::AnnotationRow> rows;
  std::vector<bool> takesFormat;
  LaneDerivation derivation;
  bitoscope::waveform_view::DecoderVisibility visibility;
  bitoscope::waveform_view::LaneDrawing drawing = bitoscope::waveform_view::LaneDrawing::Nothing;
  const ShownResult* shown = nullptr;
  double height = 0.0;
};

DecoderLaneModel decoderLaneModel(const DecoderLaneModelInput& input);

}
