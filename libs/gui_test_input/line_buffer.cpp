/*
 * SPDX-License-Identifier: MIT
 */

#include "line_buffer.h"

namespace bitoscope::gui_test_input {

std::vector<std::string> LineBuffer::push(std::string_view bytes) {
  std::vector<std::string> lines;
  if (overflowed_) {
    return lines;
  }
  for (const char c : bytes) {
    if (c == '\n') {
      if (!partial_.empty() && partial_.back() == '\r') {
        partial_.pop_back();
      }
      lines.push_back(std::move(partial_));
      partial_.clear();
      continue;
    }
    partial_.push_back(c);
    // WHY: A trailing CR may be half of a CRLF terminator, so it does not count toward the line limit.
    const std::size_t counted = partial_.size() - (partial_.back() == '\r' ? 1 : 0);
    if (counted > maxLineBytes_) {
      overflowed_ = true;
      partial_.clear();
      return lines;
    }
  }
  return lines;
}

}
