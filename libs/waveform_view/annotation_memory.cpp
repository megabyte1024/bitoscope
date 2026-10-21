/*
 * SPDX-License-Identifier: MIT
 */

#include "annotation_memory.h"

namespace bitoscope::waveform_view {

AnnotationBudget::AnnotationBudget(std::uint64_t limitBytes, std::uint64_t heldBytes)
    : limit_(limitBytes), held_(heldBytes) {}

bool AnnotationBudget::acquire(std::size_t bytes) {
  if (held_ > limit_ || bytes > limit_ - held_) return false;
  held_ += bytes;
  return true;
}

void AnnotationBudget::release(std::size_t bytes) { held_ -= bytes; }

}
