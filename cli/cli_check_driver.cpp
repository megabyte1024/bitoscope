/*
 * SPDX-License-Identifier: MIT
 */

#include "cli_check_driver.h"

#include <string>
#include <system_error>
#include <vector>

#include "capture_check.h"
#include "cli_exit_codes.h"
#include "driver_conformance.h"

namespace bitoscope::cli {

namespace {

std::string pathText(const std::filesystem::path& path) {
  const std::u8string text = path.u8string();
  return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}

}

int checkDriver(const std::filesystem::path& driver, std::ostream& out, std::ostream& err,
                std::optional<uint64_t> inactivity_timeout_ms_for_test) {
  namespace dc = driver_conformance;
  std::error_code error;
  if (!std::filesystem::is_regular_file(driver, error)) {
    err << "bitoscope_cli: no driver executable at " << pathText(driver) << std::endl;
    return kExitFailed;
  }
  const std::filesystem::path executable = std::filesystem::absolute(driver, error);
  const auto quiet = [](std::string_view) {};
  out << "checking driver " << pathText(driver) << std::endl;

  dc::ConformanceOptions lifecycle;
  lifecycle.driver = executable;
  lifecycle.onDriverStderr = quiet;
  std::vector<dc::RuleResult> results = dc::runConformance(lifecycle);
  for (const dc::RuleResult& result : results) out << dc::resultLine(result) << std::endl;

  dc::CaptureCheckOptions capture;
  capture.driver = executable;
  capture.onDriverStderr = quiet;
  capture.inactivityTimeoutMsForTest = inactivity_timeout_ms_for_test;
  const dc::CaptureCheck captured = dc::runCaptureRules(capture);
  if (!captured.target.empty()) out << captured.target << std::endl;
  for (const dc::RuleResult& result : captured.results) out << dc::resultLine(result, dc::captureRules()) << std::endl;

  results.insert(results.end(), captured.results.begin(), captured.results.end());
  out << "driver " << dc::summaryLine(results) << std::endl;
  return dc::conforms(results) ? kExitComplete : kExitFailed;
}

}
