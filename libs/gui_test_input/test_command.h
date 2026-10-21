/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <variant>

#include "frame_times.h"
#include "observation.h"

namespace bitoscope::gui_test_input {

enum class MouseButtonId : std::uint8_t { Left = 0, Right = 1, Middle = 2 };

struct PointPx {
  std::int32_t x = 0;
  std::int32_t y = 0;
  friend bool operator==(const PointPx&, const PointPx&) = default;
};

// WHY: Key ranges stay contiguous: the Dear ImGui bridge maps letters, digits, F-keys and NumPad by arithmetic.
enum class KeyId : std::uint8_t {
  None = 0,
  A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
  Digit0, Digit1, Digit2, Digit3, Digit4, Digit5, Digit6, Digit7, Digit8, Digit9,
  F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
  NumPad0, NumPad1, NumPad2, NumPad3, NumPad4, NumPad5, NumPad6, NumPad7, NumPad8, NumPad9,
  Enter, Escape, Delete, Backspace, Tab, Space,
  Left, Right, Up, Down, Home, End, PageUp, PageDown, Insert,
  Equal, Minus,
  ModCtrl, ModShift, ModAlt, ModSuper,
};

enum class PresentModeRequest : std::uint8_t { Normal, Immediate };

struct KeyChord {
  KeyId key = KeyId::None;
  bool ctrl = false;
  bool shift = false;
  bool alt = false;
  bool super = false;
  friend bool operator==(const KeyChord&, const KeyChord&) = default;
};

namespace command {

struct Move { PointPx at; };
struct Down { MouseButtonId button = MouseButtonId::Left; };
struct Up { MouseButtonId button = MouseButtonId::Left; };
struct Click { PointPx at; MouseButtonId button = MouseButtonId::Left; };
struct DoubleClick { PointPx at; };
struct Drag {
  PointPx from;
  PointPx to;
  std::uint32_t steps = 10;
};
struct Wheel { std::int32_t dx = 0; std::int32_t dy = 0; };
struct KeyPress { KeyChord chord; };
struct KeyDown { KeyChord chord; };
struct KeyUp { KeyChord chord; };
struct Text { std::string text; };
struct Frames { std::uint32_t count = 1; };
struct Wait { std::uint32_t ms = 0; };
struct Screenshot {
  std::string path;
  std::uint32_t scale = 1;
};
struct Windows {};
struct Hover {};
struct Info {};
struct Quit {};
struct Close {};

struct DialogSave { std::string path; };
struct DialogOpen { std::string path; };
struct DialogCancel {};
struct Capture {};
struct WaitUntil {
  WaitCondition condition;
  std::uint32_t timeoutMs = 0;
};
struct FrameMark {};
struct FrameTimesQuery { FrameSelection selection; };
struct PresentMode { PresentModeRequest mode = PresentModeRequest::Immediate; };

}

using Command = std::variant<command::Move, command::Down, command::Up, command::Click, command::DoubleClick,
                             command::Drag, command::Wheel, command::KeyPress, command::KeyDown,
                             command::KeyUp, command::Text, command::Frames, command::Wait,
                             command::Screenshot, command::Windows, command::Hover, command::Info,
                             command::Quit, command::Close, command::DialogSave, command::DialogOpen, command::DialogCancel,
                             command::Capture, command::WaitUntil, command::FrameMark, command::FrameTimesQuery,
                             command::PresentMode>;

constexpr std::int32_t kMaxCoordinate = 100000;
constexpr std::int32_t kMaxWheelNotches = 1000;
constexpr std::uint32_t kMaxDragSteps = 1000;
constexpr std::uint32_t kMaxFrames = 100000;
constexpr std::uint32_t kMaxWaitMs = 600000;
constexpr std::size_t kMaxTextBytes = 4096;

std::expected<Command, std::string> parseCommand(std::string_view line);

std::string_view commandName(const Command& command);

}
