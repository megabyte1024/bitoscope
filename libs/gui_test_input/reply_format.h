/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "frame_times.h"
#include "observation.h"
#include "test_command.h"

namespace bitoscope::gui_test_input {

std::string jsonString(std::string_view text);

struct RectPx {
  std::int32_t x = 0;
  std::int32_t y = 0;
  std::int32_t w = 0;
  std::int32_t h = 0;
  friend bool operator==(const RectPx&, const RectPx&) = default;
};

struct WindowInfo {
  std::string name;
  RectPx rect;
  bool focused = false;
  bool hovered = false;
  bool appearing = false;
  bool child = false;
  bool popup = false;
  bool tooltip = false;
  friend bool operator==(const WindowInfo&, const WindowInfo&) = default;
};

struct HoverInfo {
  PointPx mouse{-1, -1};
  bool mouseValid = false;
  std::string windowName;
  bool hasItem = false;
  std::uint32_t itemId = 0;
  bool wantCaptureMouse = false;
  bool wantCaptureKeyboard = false;
  bool wantTextInput = false;
  std::uint32_t activeId = 0;
  std::optional<std::int32_t> mouseCursor;
};

struct InfoReply {
  std::int32_t width = 0;
  std::int32_t height = 0;
  float scaleX = 1.0f;
  float scaleY = 1.0f;
  std::uint64_t frame = 0;
};

std::string okReply(std::string_view detail = {});
std::string errorReply(std::string_view reason);

std::string windowsReply(const std::vector<WindowInfo>& windows);
std::string captureReply(const std::vector<SessionCapture>& captures);
std::string mouseCursorName(std::int32_t cursor);

std::string hoverReply(const HoverInfo& hover);
std::string infoReply(const InfoReply& info);
std::string screenshotReply(std::string_view path, std::int32_t width, std::int32_t height);

std::string_view presentModeRequestName(PresentModeRequest request);
std::string frameMarkReply(std::uint64_t firstFrame);
std::string frameTimesReply(const FrameTimesSummary& summary, std::string_view presentMode);
std::string presentModeReply(PresentModeRequest asked, std::string_view mode);

}
