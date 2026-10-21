/*
 * SPDX-License-Identifier: MIT
 */

#include "decoder_lane_model.h"

#include <utility>

#include "decoder_labels.h"
#include "derived_lane_rows.h"
#include "lane_layout.h"
#include "row_data_format.h"

namespace bitoscope::gui_decode {

namespace decode = bitoscope::decode;
namespace wv = bitoscope::waveform_view;

void DecoderLaneLabelCache::refresh(std::span<const DecoderConfig> configs, const decode::DecoderRegistry& registry) {
  std::vector<ConfigKey> key;
  key.reserve(configs.size());
  for (const DecoderConfig& config : configs) {
    key.push_back(ConfigKey{config.decoderName(), config.id(), config.inputBindings()});
  }
  std::vector<std::string> names = registry.names();
  if (builds_ != 0 && key == configs_ && &registry == registry_ && names == registryNames_) return;
  labels_.clear();
  labels_.reserve(configs.size());
  for (std::size_t d = 0; d < configs.size(); ++d) {
    DecoderLaneLabels labels;
    if (auto created = registry.create(configs[d].decoderName())) {
      labels.declared = (*created)->rows();
      labels.rowSources = (*created)->rowSources();
    }
    labels.laneLabel = decoderLaneLabel(configs[d], configs, registry);
    labels.barName = decoderBarName(configs, d, registry);
    labels_.push_back(std::move(labels));
  }
  configs_ = std::move(key);
  registry_ = &registry;
  registryNames_ = std::move(names);
  ++builds_;
}

DecoderLaneModel decoderLaneModel(const DecoderLaneModelInput& input) {
  const DecoderConfig& config = *input.config;
  const AnnotationStore& store = *input.store;
  const std::size_t d = input.index;
  DecoderLaneModel lane;
  const ShownResult* drawnResult = store.drawn(d);
  lane.drawing = wv::laneDrawing(drawnResult != nullptr, store.progress(d) != nullptr);
  lane.shown = lane.drawing == wv::LaneDrawing::Result   ? drawnResult
               : lane.drawing == wv::LaneDrawing::Prefix ? store.progressShown(d)
                                                         : nullptr;
  const ShownResult* shown = lane.shown;
  const DecodedAnnotations* decoded = shown != nullptr ? shown->annotations.get() : nullptr;
  lane.rows = wv::shownDecoderRows(input.labels->declared,
                                   decoded != nullptr ? decoded->groups().rowIds() : std::span<const std::uint32_t>{});
  if (shown != nullptr) lane.derivation = laneDerivation(*shown, input.capture, input.showDerived);
  lane.visibility = laneVisibility(config.visibility(), lane.derivation);
  std::optional<std::vector<decode::AnnotationClass>> classes;
  const RowStripMetrics& m = input.metrics;
  for (const decode::AnnotationRow& row : lane.rows) {
    const std::vector<std::uint32_t> present = decoded != nullptr
                                                 ? wv::laneRowClasses(decoded->groups(), lane.derivation.rows, row.id)
                                                 : std::vector<std::uint32_t>{};
    const bool takesFormat =
      decoded != nullptr && rowTakesDataFormat(row.id, present, shown->format.classes, input.labels->rowSources);
    lane.takesFormat.push_back(takesFormat);
    const bool open = input.stripOpen && input.stripOpen(row.id);
    std::optional<DecoderRowStrip> strip;
    if (open) {
      if (!classes) {
        classes = shown != nullptr ? shown->format.classes
                                   : laneFormatContext(store, d, config, *input.registry).classes;
      }
      strip.emplace();
      strip->classes = wv::rowStripClasses(present, *classes);
      std::vector<double> widths;
      for (const wv::StripClass& cls : strip->classes) widths.push_back(input.stripItemWidth(cls));
      strip->layout = wv::layoutRowStrip(widths, m.width, m.gap, m.linePitch, m.padding, takesFormat);
    }
    lane.spaces.push_back(
      wv::decoderRowSpace(wv::rowHidden(config.visibility(), row.id), open, m.pitch, strip ? strip->layout.height : 0.0));
    lane.strips.push_back(std::move(strip));
  }
  lane.height = wv::decoderLaneHeight(wv::decoderRowHeights(lane.spaces), m.pitch);
  return lane;
}

}
