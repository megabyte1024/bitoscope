/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <chrono>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "device_watcher.h"
#include "plugin_connection.h"
#include "plugin_process.h"

namespace bitoscope::driver_conformance {

enum class Verdict { pass, fail, skip };

struct Rule {
  std::string_view id;
  bool required = true;
  std::string_view description;
};

std::span<const Rule> rules();

struct RuleResult {
  std::string rule;
  bool required = true;
  Verdict verdict = Verdict::pass;
  std::vector<std::string> problems;
  std::string skipReason;
};

struct ConformanceOptions {
  std::filesystem::path driver;
  host::DriverConnection connection;
  std::chrono::milliseconds timeout = host::kDefaultRequestTimeout;
  std::chrono::milliseconds listAgainWait{2000};
  std::chrono::milliseconds exitWait = host::kExitWait;
  host::StderrLineFn onDriverStderr;
};

std::vector<RuleResult> runConformance(const ConformanceOptions& options);

bool conforms(std::span<const RuleResult> results);

std::string resultLine(const RuleResult& result, std::span<const Rule> table = rules());

std::string summaryLine(std::span<const RuleResult> results);

}
