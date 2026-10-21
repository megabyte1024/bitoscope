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
#include "decoder_row_strip.h"
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

struct DecoderRowStrip {
  std::vector<bitoscope::waveform_view::StripClass> classes;
  bitoscope::waveform_view::RowStripLayout layout;
  bool operator==(const DecoderRowStrip& other) const {
    return classes == other.classes && layout.classItems == other.layout.classItems &&
           layout.formatLine == other.layout.formatLine && layout.lines == other.layout.lines &&
           layout.height == other.layout.height;
  }
};

struct RowStripMetrics {
  double pitch = 0.0;
  double width = 0.0;
  double gap = 0.0;
  double linePitch = 0.0;
  double padding = 0.0;
};

struct DecoderLaneModelInput {
  const DecoderConfig* config = nullptr;
  const AnnotationStore* store = nullptr;
  std::size_t index = 0;
  const DecoderLaneLabels* labels = nullptr;
  const bitoscope::decode::DecoderRegistry* registry = nullptr;
  const bitoscope::decode::SignalView* capture = nullptr;
  bool showDerived = true;
  std::function<bool(std::uint32_t row)> stripOpen;
  std::function<double(const bitoscope::waveform_view::StripClass& cls)> stripItemWidth;
  RowStripMetrics metrics;
};

struct DecoderLaneModel {
  std::vector<bitoscope::decode::AnnotationRow> rows;
  std::vector<bitoscope::waveform_view::DecoderRowSpace> spaces;
  std::vector<std::optional<DecoderRowStrip>> strips;
  std::vector<bool> takesFormat;
  LaneDerivation derivation;
  bitoscope::waveform_view::DecoderVisibility visibility;
  bitoscope::waveform_view::LaneDrawing drawing = bitoscope::waveform_view::LaneDrawing::Nothing;
  const ShownResult* shown = nullptr;
  double height = 0.0;
};

DecoderLaneModel decoderLaneModel(const DecoderLaneModelInput& input);

}
