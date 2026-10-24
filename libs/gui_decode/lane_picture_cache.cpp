/*
 * SPDX-License-Identifier: MIT
 */

#include "lane_picture_cache.h"

#include <utility>
#include <variant>

namespace bitoscope::gui_decode {

namespace decode = bitoscope::decode;
namespace wv = bitoscope::waveform_view;

namespace {

DerivedRowKey derivedRowKey(const wv::DerivedRowSource& source) {
  DerivedRowKey key;
  key.row = source.row;
  key.cls = source.cls;
  key.sourceGroups = source.sourceGroups;
  key.rule = source.rule.index();
  if (const auto* grid = std::get_if<decode::BitGrid>(&source.rule)) {
    key.gridRateMilliHz = grid->sample_rate_milli_hz;
    key.gridBitsPerSecond = grid->bits_per_second;
    key.gridOffsetBits = grid->offset_bits;
    key.gridCount = grid->count;
    key.gridMsbFirst = grid->msb_first;
  } else if (const auto* edges = std::get_if<wv::EdgeRule>(&source.rule)) {
    key.clockChannel = edges->edges.clock_channel;
    key.dataChannel = edges->edges.data_channel;
    key.edgeCount = edges->edges.count;
    key.view = edges->view;
  }
  return key;
}

}

bool LanePictureKey::operator==(const LanePictureKey& other) const {
  return resultSerial == other.resultSerial && drawing == other.drawing && watermark == other.watermark &&
         derived == other.derived && captureSerial == other.captureSerial && visibility == other.visibility &&
         rows == other.rows && classes == other.classes && options == other.options &&
         hexStyle == other.hexStyle && rowDataFormats == other.rowDataFormats &&
         rowSources == other.rowSources && fontSerial == other.fontSerial &&
         firstVisibleSample == other.firstVisibleSample && samplesPerPixel == other.samplesPerPixel &&
         columnCount == other.columnCount && geometry == other.geometry && style == other.style &&
         pack == other.pack;
}

LanePictureKey lanePictureKey(const LanePictureSources& sources) {
  const DecoderLaneModel& lane = *sources.lane;
  const ShownResult* shown = lane.shown;
  LanePictureKey key;
  key.resultSerial = shown != nullptr && shown->annotations != nullptr ? shown->annotations->serial() : 0;
  key.drawing = lane.drawing;
  key.watermark = sources.watermark;
  for (const wv::DerivedRowSource& source : lane.derivation.rows.rows) key.derived.push_back(derivedRowKey(source));
  key.captureSerial = sources.captureSerial;
  key.visibility = lane.visibility;
  key.rows = lane.rows;
  if (shown != nullptr) {
    key.classes = shown->format.classes;
    key.options = shown->format.options;
  }
  key.hexStyle = sources.hexStyle;
  key.rowDataFormats = sources.config->rowDataFormats();
  key.rowSources = sources.labels->rowSources;
  key.fontSerial = sources.fontSerial;
  key.firstVisibleSample = sources.columns.firstVisibleSample();
  key.samplesPerPixel = sources.columns.samplesPerPixel();
  key.columnCount = sources.columnCount;
  key.geometry = sources.geometry;
  key.style = sources.style;
  key.pack = sources.pack;
  return key;
}

wv::LanePictureInput lanePictureInput(const LanePictureSources& sources) {
  static const wv::AnnotationGroups kNoGroups;
  const DecoderLaneModel& lane = *sources.lane;
  const ShownResult* shown = lane.shown;
  wv::LanePictureInput input;
  if (shown != nullptr) {
    input.groups = shown->annotations != nullptr ? &shown->annotations->groups() : &kNoGroups;
    // WHY: Use the shown result's own format context: an option changed during a recompute must not reformat it.
    input.classes = shown->format.classes;
  }
  input.derived = &lane.derivation.rows;
  input.rows = lane.rows;
  input.visibility = &lane.visibility;
  input.drawing = lane.drawing;
  input.watermark = sources.watermark;
  input.columns = sources.columns;
  input.columnCount = sources.columnCount;
  input.geometry = sources.geometry;
  input.style = sources.style;
  input.pack = sources.pack;
  input.text = sources.text;
  return input;
}

const wv::LanePicture& LanePictureCache::picture(const LanePictureSources& sources) {
  LanePictureKey key = lanePictureKey(sources);
  if (key_ && *key_ == key) return picture_;
  picture_ = wv::drawLanePicture(lanePictureInput(sources));
  key_ = std::move(key);
  ++builds_;
  return picture_;
}

}
