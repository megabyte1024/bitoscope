/*
 * SPDX-License-Identifier: MIT
 */

#include "status_line.h"

#include <format>

#include "capture_plan.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/ioctl.h>
#include <unistd.h>
#endif

namespace bitoscope::cli {

namespace {

namespace pd = bitoscope::capture::plan_detail;

uint64_t percentOf(uint64_t part, uint64_t whole) {
  if (whole == 0 || part >= whole) return 100;
  const auto percent = static_cast<uint64_t>(static_cast<double>(part) * 100.0 /
                                             static_cast<double>(whole));
  return percent > 99 ? 99 : percent;
}

std::string seconds(uint64_t ms) { return std::format("{}.{} s", ms / 1000, ms % 1000 / 100); }

constexpr uint32_t kDefaultColumns = 80;

}

std::string formatStatusLine(const StatusFigures& f) {
  std::string line;
  if (f.armed.has_value()) {
    const StatusFigures::Armed& a = *f.armed;
    line = "trigger armed " + seconds(a.waited_ms);
    if (a.timeout_s != 0) line += std::format(" of {} s", a.timeout_s);
    line += a.history_complete ? ", pre-trigger ready" : ", pre-trigger filling";
  } else {
    line = std::format("{}captured {} of {} ({}%)", f.triggered ? "triggered, " : "",
                       pd::withThousands(f.stored_samples), pd::withThousands(f.requested_samples),
                       percentOf(f.stored_samples, f.requested_samples));
  }
  line += " | memory " + pd::formatSize(f.memory_used_bytes) + " of " +
          pd::formatSize(f.memory_bytes);
  line += std::format(" | ring {}/{}", f.ring_slots_in_use, f.ring_slots);
  return line;
}

StatusLine::StatusLine(std::ostream* out, std::optional<uint32_t> columns, uint64_t interval_ms)
    : out_(columns.has_value() ? out : nullptr),
      columns_(columns.value_or(0)),
      interval_ms_(interval_ms) {}

StatusLine::~StatusLine() { clear(); }

bool StatusLine::due(uint64_t now_ms) const {
  if (out_ == nullptr) return false;
  return !drawn_ || now_ms - last_draw_ms_ >= interval_ms_;
}

void StatusLine::draw(uint64_t now_ms, std::string_view text) {
  if (out_ == nullptr) return;
  const size_t width = columns_ > 1 ? columns_ - 1 : 1;
  if (text.size() > width) text = text.substr(0, width);
  std::string line = "\r";
  line += text;
  if (shown_ > text.size()) line.append(shown_ - text.size(), ' ');
  *out_ << line << std::flush;
  shown_ = text.size() > shown_ ? text.size() : shown_;
  drawn_ = true;
  last_draw_ms_ = now_ms;
}

void StatusLine::clear() {
  if (out_ == nullptr || !drawn_) return;
  std::string blank = "\r";
  blank.append(shown_, ' ');
  blank += '\r';
  *out_ << blank << std::flush;
  shown_ = 0;
  drawn_ = false;
}

std::optional<uint32_t> stderrTerminalColumns() {
#if defined(_WIN32)
  const HANDLE handle = GetStdHandle(STD_ERROR_HANDLE);
  if (handle == INVALID_HANDLE_VALUE || handle == nullptr) return std::nullopt;
  DWORD mode = 0;
  // WHY: GetConsoleMode fails for file, pipe and NUL: _isatty says yes for NUL, so it is not used.
  if (GetConsoleMode(handle, &mode) == 0) return std::nullopt;
  CONSOLE_SCREEN_BUFFER_INFO info{};
  if (GetConsoleScreenBufferInfo(handle, &info) != 0) {
    const int width = info.srWindow.Right - info.srWindow.Left + 1;
    if (width > 0) return static_cast<uint32_t>(width);
  }
  return kDefaultColumns;
#else
  if (isatty(STDERR_FILENO) == 0) return std::nullopt;
  winsize size{};
  if (ioctl(STDERR_FILENO, TIOCGWINSZ, &size) == 0 && size.ws_col > 0) {
    return static_cast<uint32_t>(size.ws_col);
  }
  return kDefaultColumns;
#endif
}

}
