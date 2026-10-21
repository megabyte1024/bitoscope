/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>

#include "progress.h"

namespace bitoscope::cli {

// WHY: 250 ms is every fifth 50 ms acquisition poll, so redraws land at even rate.
inline constexpr uint64_t kStatusLineIntervalMs = 250;

using StatusFigures = bitoscope::acquisition::Progress;

std::string formatStatusLine(const StatusFigures& figures);

class StatusLine {
 public:
  StatusLine(std::ostream* out, std::optional<uint32_t> columns,
             uint64_t interval_ms = kStatusLineIntervalMs);
  ~StatusLine();

  StatusLine(const StatusLine&) = delete;
  StatusLine& operator=(const StatusLine&) = delete;

  bool enabled() const { return out_ != nullptr; }
  bool due(uint64_t now_ms) const;
  void draw(uint64_t now_ms, std::string_view text);
  void clear();

 private:
  std::ostream* out_ = nullptr;
  uint32_t columns_ = 0;
  uint64_t interval_ms_ = 0;
  bool drawn_ = false;
  uint64_t last_draw_ms_ = 0;
  size_t shown_ = 0;
};

std::optional<uint32_t> stderrTerminalColumns();

}
