/*
 * SPDX-License-Identifier: MIT
 */

#include "lane_coverage.h"

namespace bitoscope::waveform_view {

LaneCoverage::LaneCoverage(const AnnotationGroups& groups, double firstVisibleSample, double samplesPerPixel,
                           std::uint32_t widthPixels)
    : groups_(&groups), first_(firstVisibleSample), spp_(samplesPerPixel), width_(widthPixels),
      bounds_(groups.groupCount()) {}

bool LaneCoverage::decides(std::size_t g) const {
  const CoveragePyramid& pyramid = groups_->coverage(g);
  return !pyramid.empty() && width_ != 0 &&
         spp_ >= static_cast<double>(kLeastBucketsInColumn) * static_cast<double>(pyramid.bucketSamples());
}

const CoverageBounds& LaneCoverage::at(std::size_t g, std::uint32_t column) const {
  std::vector<CoverageBounds>& bounds = bounds_[g];
  if (bounds.empty()) {
    bounds.resize(width_);
    groups_->coverage(g).columnBounds(first_, spp_, bounds);
  }
  return bounds[column];
}

}
