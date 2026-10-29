/*
 * SPDX-License-Identifier: MIT
 */

#include "view_table.h"

#include <format>
#include <utility>

namespace bitoscope::gui_waveform_frame {

UploadChange WaveformViewTable::upload(uint64_t view, uint32_t width, uint32_t rows, std::size_t levelBytes) {
  const bool fits = width != 0 && rows != 0 && levelBytes == static_cast<std::size_t>(width) * rows;
  if (!fits) {
    shapes_.erase(view);
    return UploadChange::Rejected;
  }
  if (width > kMaxViewEdge || rows > kMaxViewEdge) {
    shapes_.erase(view);
    problem(std::format("Waveform pass: view of {} x {} columns and rows is larger than it may be ({})", width, rows,
                        kMaxViewEdge));
    return UploadChange::TooLarge;
  }
  ViewShape& shape = shapes_[view];
  const bool sameShape = shape.generation != 0 && shape.width == width && shape.rows == rows;
  shape.width = width;
  shape.rows = rows;
  ++shape.generation;
  return sameShape ? UploadChange::Refreshed : UploadChange::Reshaped;
}

void WaveformViewTable::release(uint64_t view) { shapes_.erase(view); }

bool WaveformViewTable::holds(uint64_t view, uint32_t width, uint32_t rows) const {
  const ViewShape* shape = find(view);
  return shape != nullptr && shape->width == width && shape->rows == rows;
}

const ViewShape* WaveformViewTable::find(uint64_t view) const {
  const auto it = shapes_.find(view);
  return it == shapes_.end() ? nullptr : &it->second;
}

void WaveformViewTable::problem(std::string line) {
  if (problems_.empty() || problems_.back() != line) {
    problems_.push_back(std::move(line));
  }
}

std::vector<std::string> WaveformViewTable::takeProblems() {
  std::vector<std::string> out;
  out.swap(problems_);
  return out;
}

}
