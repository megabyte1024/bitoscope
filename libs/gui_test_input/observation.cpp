/*
 * SPDX-License-Identifier: MIT
 */

#include "observation.h"

#include <format>

namespace bitoscope::gui_test_input {

namespace {

constexpr std::string_view kIdMarker = "###";
constexpr std::string_view kLabelMarker = "##";

bool isInProgress(std::string_view state) { return state == kCaptureRunning || state == kCaptureWaitingForTrigger; }

bool hasEnded(std::string_view state) { return state == kCaptureDone || state == kCaptureFailed; }

bool anyWindowMatches(const Observation& observation, std::string_view wanted) {
  for (const std::string& name : observation.windowNames) {
    if (windowNameMatches(name, wanted)) {
      return true;
    }
  }
  return false;
}

}

bool windowNameMatches(std::string_view windowName, std::string_view wanted) {
  if (wanted.empty()) {
    return false;
  }
  if (windowName == wanted) {
    return true;
  }
  const std::size_t labelEnd = windowName.find(kLabelMarker);
  if (labelEnd != std::string_view::npos && windowName.substr(0, labelEnd) == wanted) {
    return true;
  }
  const std::size_t idStart = windowName.find(kIdMarker);
  if (idStart != std::string_view::npos) {
    std::string_view id = windowName.substr(idStart + kIdMarker.size());
    const std::size_t idEnd = id.find(kIdMarker);
    if (idEnd != std::string_view::npos) {
      id = id.substr(0, idEnd);
    }
    if (id == wanted) {
      return true;
    }
  }
  return false;
}

bool conditionHolds(const WaitCondition& condition, const Observation& observation) {
  switch (condition.kind) {
  case WaitCondition::Kind::WindowOpen:
    return anyWindowMatches(observation, condition.name);
  case WaitCondition::Kind::WindowClosed:
    return !anyWindowMatches(observation, condition.name);
  case WaitCondition::Kind::CaptureDone: {
    bool ended = false;
    for (const SessionCapture& capture : observation.captures) {
      if (isInProgress(capture.state)) {
        return false;
      }
      ended = ended || hasEnded(capture.state);
    }
    return ended;
  }
  }
  return false;
}

std::string describeCondition(const WaitCondition& condition, const Observation& observation) {
  switch (condition.kind) {
  case WaitCondition::Kind::WindowOpen:
    return std::format("window '{}'", condition.name);
  case WaitCondition::Kind::WindowClosed:
    return std::format("no-window '{}'", condition.name);
  case WaitCondition::Kind::CaptureDone: {
    if (observation.captures.empty()) {
      return "capture-done (no panel has a capture)";
    }
    std::string states;
    for (const SessionCapture& capture : observation.captures) {
      states += std::format("{}{}: {}", states.empty() ? "" : ", ", capture.name, capture.state);
    }
    return std::format("capture-done ({})", states);
  }
  }
  return {};
}

}
