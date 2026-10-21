/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <unordered_map>

namespace bitoscope::waveform_view {

class PassUploads {
 public:
  void rowsMade() { ++generation_; }
  void uploaded(std::uint64_t view);
  bool needed(std::uint64_t view, bool passHoldsShape) const;

 private:
  std::uint64_t generation_ = 0;
  std::unordered_map<std::uint64_t, std::uint64_t> uploadedAt_;
};

}
