/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "decoder.h"
#include "decoder_config.h"
#include "decoder_lane_model.h"
#include "decoder_lane_picture.h"
#include "decoder_visibility.h"
#include "number_format.h"
#include "row_source.h"
#include "viewport.h"

namespace bitoscope::gui_decode {

struct LanePictureSources {
  const DecoderLaneModel* lane = nullptr;
  const DecoderConfig* config = nullptr;
  const DecoderLaneLabels* labels = nullptr;
  bitoscope::decode::HexStyle hexStyle = bitoscope::decode::HexStyle::Prefixed;
  std::uint64_t captureSerial = 0;
  std::uint64_t fontSerial = 0;
  std::uint64_t watermark = 0;
  bitoscope::waveform_view::Viewport columns{1, 1};
  std::uint32_t columnCount = 1;
  bitoscope::waveform_view::LanePictureGeometry geometry;
  bitoscope::waveform_view::LanePictureStyle style;
  bitoscope::waveform_view::PackColor pack = nullptr;
  const bitoscope::waveform_view::LaneTextSource* text = nullptr;
};

struct DerivedRowKey {
  std::uint32_t row = 0;
  std::uint32_t cls = 0;
  std::vector<std::size_t> sourceGroups;
  std::size_t rule = 0;
  std::uint64_t gridRateMilliHz = 0;
  std::uint64_t gridBitsPerSecond = 0;
  std::uint32_t gridOffsetBits = 0;
  std::uint32_t gridCount = 0;
  bool gridMsbFirst = false;
  std::uint32_t clockChannel = 0;
  std::uint32_t dataChannel = 0;
  std::uint32_t edgeCount = 0;
  const bitoscope::decode::SignalView* view = nullptr;
  bool operator==(const DerivedRowKey&) const = default;
};

struct LanePictureKey {
  std::uint64_t resultSerial = 0;
  bitoscope::waveform_view::LaneDrawing drawing = bitoscope::waveform_view::LaneDrawing::Nothing;
  std::uint64_t watermark = 0;
  std::vector<DerivedRowKey> derived;
  std::uint64_t captureSerial = 0;
  bitoscope::waveform_view::DecoderVisibility visibility;
  std::vector<bitoscope::decode::AnnotationRow> rows;
  std::vector<bitoscope::decode::AnnotationClass> classes;
  bitoscope::decode::OptionMap options;
  std::map<bitoscope::waveform_view::RowClass, bitoscope::waveform_view::ColorChoice> classColors;
  bitoscope::decode::HexStyle hexStyle = bitoscope::decode::HexStyle::Prefixed;
  std::map<std::uint32_t, bitoscope::decode::DataFormat> rowDataFormats;
  std::vector<bitoscope::decode::RowSource> rowSources;
  std::uint64_t fontSerial = 0;
  double firstVisibleSample = 0.0;
  double samplesPerPixel = 0.0;
  std::uint32_t columnCount = 0;
  bitoscope::waveform_view::LanePictureGeometry geometry;
  bitoscope::waveform_view::LanePictureStyle style;
  bitoscope::waveform_view::PackColor pack = nullptr;
  bool operator==(const LanePictureKey& other) const;
};

LanePictureKey lanePictureKey(const LanePictureSources& sources);

bitoscope::waveform_view::LanePictureInput lanePictureInput(const LanePictureSources& sources);

class LanePictureCache {
 public:
  const bitoscope::waveform_view::LanePicture& picture(const LanePictureSources& sources);
  std::size_t builds() const { return builds_; }

 private:
  std::optional<LanePictureKey> key_;
  bitoscope::waveform_view::LanePicture picture_;
  std::size_t builds_ = 0;
};

}
