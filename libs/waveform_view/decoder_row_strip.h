/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "annotation_reader.h"
#include "annotation_shape.h"
#include "decoder.h"
#include "decoder_visibility.h"

namespace bitoscope::waveform_view {

struct RowLayout {
  std::vector<decode::Annotation> annotations;
  std::vector<RowMark> marks;
  std::string failure;
  bool operator==(const RowLayout&) const = default;
};

std::vector<RowLayout> layoutAnnotationRows(const AnnotationGroups& groups, const AnnotationRange& range,
                                            std::span<const decode::AnnotationRow> rows,
                                            const DecoderVisibility& visibility, double firstVisibleSample,
                                            double samplesPerPixel, std::uint32_t widthPixels);
std::vector<RowLayout> layoutAnnotationRows(AnnotationReader& reader, const AnnotationRange& range,
                                            std::span<const decode::AnnotationRow> rows,
                                            const DecoderVisibility& visibility, double firstVisibleSample,
                                            double samplesPerPixel, std::uint32_t widthPixels);

std::string classDisplayName(const decode::AnnotationClass& cls);

struct StripClass {
  std::uint32_t id = 0;
  std::string name;
  bool operator==(const StripClass&) const = default;
};

std::vector<StripClass> rowStripClasses(std::span<const std::uint32_t> presentIds,
                                        std::span<const decode::AnnotationClass> classes);

// WHY: A hidden row takes half a row pitch: enough for one line of its greyed name and triangle at 1.8 em pitch.
inline constexpr double kHiddenRowPitchFraction = 0.5;

struct DecoderRowSpace {
  double band = 0.0;
  double strip = 0.0;
  double height() const { return band + strip; }
  bool operator==(const DecoderRowSpace&) const = default;
};

DecoderRowSpace decoderRowSpace(bool rowHidden, bool stripOpen, double rowPitch, double stripHeight);

std::vector<double> decoderRowHeights(std::span<const DecoderRowSpace> rows);

struct DecoderRowHit {
  std::size_t slot = 0;
  bool inStrip = false;
  bool operator==(const DecoderRowHit&) const = default;
};
std::optional<DecoderRowHit> decoderRowHitAtY(double y, double laneTop, std::span<const DecoderRowSpace> rows);

struct StripItem {
  double x = 0.0;
  std::size_t line = 0;
  bool operator==(const StripItem&) const = default;
};
struct RowStripLayout {
  std::vector<StripItem> classItems;
  std::optional<std::size_t> formatLine;
  std::size_t lines = 1;
  double height = 0.0;
};
RowStripLayout layoutRowStrip(std::span<const double> classItemWidths, double width, double gap, double linePitch,
                              double padding, bool formatLine = false);

}
