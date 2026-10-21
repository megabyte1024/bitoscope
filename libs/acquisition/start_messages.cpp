/*
 * SPDX-License-Identifier: MIT
 */

#include "start_messages.h"

#include <format>

namespace bitoscope::acquisition {

std::string couldNotStartCapture(std::string_view id, std::string_view why) {
  return std::format("could not start capture on '{}': {}", id, why);
}

std::string noCaptureIdMessage() { return "cannot start capture: no capture id could be generated"; }

std::string couldNotSetUpRing(std::string_view id, std::string_view why) {
  return std::format("could not set up bulk ring for capture on '{}' - nothing was started: {}", id, why);
}

std::string ringNotCreatedMessage() { return "bulk ring was not created"; }

std::string propertiesNotSet(std::string_view who, const bitoscope::host::PropertiesRefusal& refusal) {
  const bool name_it = !refusal.property.empty() && refusal.message.find(refusal.property) == std::string::npos;
  const std::string suffix = name_it ? std::format(" (property '{}')", refusal.property) : std::string();
  return std::format("could not set '{}' properties - capture was not started: {}{}", who, refusal.message, suffix);
}

std::string mayStillBeRunning(std::string_view id, std::string_view why) {
  return std::format("capture may still be running on '{}' - stopping it failed: {}", id, why);
}

}
