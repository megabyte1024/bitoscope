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

}
