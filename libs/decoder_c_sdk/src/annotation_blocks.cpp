/*
 * SPDX-License-Identifier: MIT
 */

#include "annotation_blocks.h"

#include <algorithm>
#include <cstring>

namespace bitoscope::plugin_c_sdk {

void AnnotationBlocks::append(std::span<const bitoscope_annotation> batch) {
  size_t at = 0;
  while (at < batch.size()) {
    const size_t filled = size_ % kBlockAnnotations;
    if (filled == 0 && size_ / kBlockAnnotations == blocks_.size()) {
      blocks_.push_back(std::make_unique_for_overwrite<bitoscope_annotation[]>(kBlockAnnotations));
    }
    const size_t n = (std::min)(kBlockAnnotations - filled, batch.size() - at);
    std::memcpy(blocks_.back().get() + filled, batch.data() + at, n * sizeof(bitoscope_annotation));
    size_ += n;
    at += n;
  }
}

std::vector<bitoscope_annotation> AnnotationBlocks::all() const {
  std::vector<bitoscope_annotation> out;
  out.reserve(size_);
  for (size_t b = 0; b < blocks_.size(); ++b) {
    const size_t n = (std::min)(kBlockAnnotations, size_ - b * kBlockAnnotations);
    out.insert(out.end(), blocks_[b].get(), blocks_[b].get() + n);
  }
  return out;
}

}
