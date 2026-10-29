/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace bitoscope::gui_waveform_frame {

inline constexpr uint32_t kMaxViewEdge = 8192;

struct ViewShape {
  uint32_t width = 0;
  uint32_t rows = 0;
  uint64_t generation = 0;
};

enum class UploadChange : uint8_t { Rejected, TooLarge, Reshaped, Refreshed };

class WaveformViewTable {
 public:
  UploadChange upload(uint64_t view, uint32_t width, uint32_t rows, std::size_t levelBytes);
  void release(uint64_t view);

  bool holds(uint64_t view, uint32_t width, uint32_t rows) const;
  const ViewShape* find(uint64_t view) const;

  void problem(std::string line);
  std::vector<std::string> takeProblems();

 private:
  std::map<uint64_t, ViewShape> shapes_;
  std::vector<std::string> problems_;
};

}
