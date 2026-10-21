/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace bitoscope::gui_test_input {

class LineBuffer {
 public:
  explicit LineBuffer(std::size_t maxLineBytes = 8192) : maxLineBytes_(maxLineBytes) {}

  std::vector<std::string> push(std::string_view bytes);

  bool overflowed() const { return overflowed_; }

 private:
  std::size_t maxLineBytes_;
  std::string partial_;
  bool overflowed_ = false;
};

}
