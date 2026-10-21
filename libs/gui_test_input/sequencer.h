/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

// WHY: One step per frame: Dear ImGui trickles input, so move and press, or press and release, in one frame
// is seen late or as a press only.

#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <vector>

#include "coordinates.h"
#include "observation.h"
#include "test_command.h"

namespace bitoscope::gui_test_input {

struct InputEvent {
  enum class Kind : std::uint8_t { MousePos, MouseButton, MouseWheel, Key, Text };
  Kind kind = Kind::MousePos;
  float x = 0.0f;
  float y = 0.0f;
  MouseButtonId button = MouseButtonId::Left;
  KeyId key = KeyId::None;
  bool down = false;
  std::string text;
  friend bool operator==(const InputEvent&, const InputEvent&) = default;
};

struct FrameQuery {
  enum class Kind : std::uint8_t {
    Screenshot,
    Windows,
    Hover,
    Info,
    Capture,
    DialogSave,
    DialogOpen,
    DialogCancel,
    FrameMark,
    FrameTimes,
    PresentMode,
  };
  Kind kind = Kind::Info;
  std::string path;
  std::uint32_t scale = 1;
  FrameSelection selection;
  PresentModeRequest presentMode = PresentModeRequest::Immediate;
  friend bool operator==(const FrameQuery&, const FrameQuery&) = default;
};

struct FramePlan {
  std::vector<InputEvent> events;
  std::optional<FrameQuery> query;
  bool quit = false;
  bool close = false;
};

struct Reply {
  std::uint64_t ticket = 0;
  std::string text;
};

class Sequencer {
 public:
  void push(std::uint64_t ticket, Command command);

  bool idle() const;

  FramePlan beginFrame(std::uint64_t nowMs, CoordinateScale scale);

  std::vector<Reply> endFrame(std::uint64_t nowMs, std::optional<std::string> queryReply,
                              const Observation& observation = {});

 private:
  struct Step {
    std::vector<InputEvent> events;
    std::optional<FrameQuery> query;
    bool quit = false;
    bool close = false;
  };
  struct Running {
    std::uint64_t ticket = 0;
    Command command;
    std::vector<Step> steps;
    std::size_t next = 0;
    std::uint32_t frames = 0;
    std::optional<std::uint64_t> startMs;
    bool queryPending = false;
    bool waitOnly = false;
  };
  struct Queued {
    std::uint64_t ticket = 0;
    Command command;
  };

  static std::vector<Step> expand(const Command& command, CoordinateScale scale);

  std::deque<Queued> queue_;
  std::optional<Running> running_;
};

}
