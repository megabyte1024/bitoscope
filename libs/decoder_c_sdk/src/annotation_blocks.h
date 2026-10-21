/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstddef>
#include <memory>
#include <span>
#include <vector>

#include "bitoscope_decoder.h"

namespace bitoscope::plugin_c_sdk {

class AnnotationBlocks {
 public:
  static constexpr size_t kBlockAnnotations = 2048;

  void append(std::span<const bitoscope_annotation> batch);
  size_t size() const { return size_; }
  const bitoscope_annotation& operator[](size_t index) const {
    return blocks_[index / kBlockAnnotations][index % kBlockAnnotations];
  }
  std::vector<bitoscope_annotation> all() const;

 private:
  std::vector<std::unique_ptr<bitoscope_annotation[]>> blocks_;
  size_t size_ = 0;
};

}
