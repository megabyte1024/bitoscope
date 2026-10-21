/*
 * SPDX-License-Identifier: MIT
 */

#include "pass_uploads.h"

namespace bitoscope::waveform_view {

void PassUploads::uploaded(std::uint64_t view) {
  uploadedAt_[view] = generation_;
}

bool PassUploads::needed(std::uint64_t view, bool passHoldsShape) const {
  const auto it = uploadedAt_.find(view);
  return !passHoldsShape || it == uploadedAt_.end() || it->second != generation_;
}

}
