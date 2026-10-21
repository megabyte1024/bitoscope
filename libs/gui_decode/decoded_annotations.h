/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "annotation_groups.h"
#include "decoder.h"

namespace bitoscope::gui_decode {

class DecodedAnnotations {
 public:
  explicit DecodedAnnotations(std::vector<bitoscope::decode::Annotation> annotations);
  explicit DecodedAnnotations(bitoscope::waveform_view::AnnotationGroups groups);

  std::size_t size() const { return groups_.size(); }
  const bitoscope::waveform_view::AnnotationGroups& groups() const { return groups_; }
  std::uint64_t serial() const { return serial_; }

 private:
  bitoscope::waveform_view::AnnotationGroups groups_;
  std::uint64_t serial_ = 0;
};

}
