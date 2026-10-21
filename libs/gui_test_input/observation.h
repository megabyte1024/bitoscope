/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace bitoscope::gui_test_input {

inline constexpr std::string_view kCaptureIdle = "idle";
inline constexpr std::string_view kCaptureWaitingForTrigger = "waiting-for-trigger";
inline constexpr std::string_view kCaptureRunning = "running";
inline constexpr std::string_view kCaptureDone = "done";
inline constexpr std::string_view kCaptureFailed = "failed";

struct SessionCapture {
  std::string name;
  std::string state;
  std::uint64_t stored = 0;
  std::uint64_t requested = 0;
  std::string reason;
  friend bool operator==(const SessionCapture&, const SessionCapture&) = default;
};

struct Observation {
  std::vector<std::string> windowNames;
  std::vector<SessionCapture> captures;
};

struct WaitCondition {
  enum class Kind : std::uint8_t {
    CaptureDone,
    WindowOpen,
    WindowClosed,
  };
  Kind kind = Kind::CaptureDone;
  std::string name;
  friend bool operator==(const WaitCondition&, const WaitCondition&) = default;
};

bool windowNameMatches(std::string_view windowName, std::string_view wanted);

bool conditionHolds(const WaitCondition& condition, const Observation& observation);

std::string describeCondition(const WaitCondition& condition, const Observation& observation);

}
