/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstddef>
#include <functional>
#include <span>
#include <string>
#include <string_view>

#include "annotation_store.h"
#include "decoder.h"
#include "decoder_config.h"
#include "decoder_lane_picture.h"
#include "number_format.h"
#include "row_source.h"

namespace bitoscope::gui_decode {

class AnnotationTextSource final : public bitoscope::waveform_view::LaneTextSource {
 public:
  AnnotationTextSource(const ShownResult* shown, const DecoderConfig& config, bitoscope::decode::HexStyle hexStyle,
                       std::span<const bitoscope::decode::RowSource> rowSources,
                       std::function<float(std::string_view)> measure);

  std::string text(const bitoscope::decode::Annotation& annotation) const override;
  bool listsWords(const bitoscope::decode::Annotation& annotation) const override;
  std::string wordList(const bitoscope::decode::Annotation& annotation, std::size_t maxChars) const override;
  float width(std::string_view text) const override;

 private:
  bitoscope::decode::NumberFormat formatOf(const bitoscope::decode::Annotation& annotation) const;

  const ShownResult* shown_;
  const DecoderConfig& config_;
  bitoscope::decode::HexStyle hexStyle_;
  std::vector<bitoscope::decode::RowSource> rowSources_;
  std::function<float(std::string_view)> measure_;
};

}
