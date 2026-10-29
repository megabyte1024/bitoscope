/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "driver_conformance.h"

namespace bitoscope::driver_conformance {

inline constexpr std::string_view kTestPatternProperty = "test_pattern";

std::span<const Rule> captureRules();

struct CaptureCheckOptions {
  std::filesystem::path driver;
  host::StderrLineFn onDriverStderr;
  std::optional<uint64_t> inactivityTimeoutMsForTest;
  std::optional<uint64_t> stopSamplesForTest;
};

struct CaptureCheck {
  std::string target;
  std::vector<RuleResult> results;
};

CaptureCheck runCaptureRules(const CaptureCheckOptions& options);

}
