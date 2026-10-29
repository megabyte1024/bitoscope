/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "annotation_groups.h"
#include "coverage_pyramid.h"

namespace bitoscope::waveform_view {

class LaneCoverage {
 public:
  static constexpr std::uint64_t kLeastBucketsInColumn = 2;

  LaneCoverage(const AnnotationGroups& groups, double firstVisibleSample, double samplesPerPixel,
               std::uint32_t widthPixels);

  bool decides(std::size_t g) const;
  const CoverageBounds& at(std::size_t g, std::uint32_t column) const;

 private:
  const AnnotationGroups* groups_;
  double first_;
  double spp_;
  std::uint32_t width_;
  mutable std::vector<std::vector<CoverageBounds>> bounds_;
};

}
