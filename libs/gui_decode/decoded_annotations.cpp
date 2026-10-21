/*
 * SPDX-License-Identifier: MIT
 */

#include "decoded_annotations.h"

#include <atomic>
#include <utility>

namespace bitoscope::gui_decode {

namespace {

std::uint64_t nextSerial() {
  static std::atomic<std::uint64_t> last{0};
  return last.fetch_add(1, std::memory_order_relaxed) + 1;
}

}

DecodedAnnotations::DecodedAnnotations(std::vector<bitoscope::decode::Annotation> annotations)
    : groups_(bitoscope::waveform_view::groupAnnotations(annotations)), serial_(nextSerial()) {}

DecodedAnnotations::DecodedAnnotations(bitoscope::waveform_view::AnnotationGroups groups)
    : groups_(std::move(groups)), serial_(nextSerial()) {}

}
