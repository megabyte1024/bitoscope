/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <expected>
#include <functional>
#include <optional>
#include <ostream>
#include <string>

#include "acquisition.h"
#include "cli_args.h"
// WHY: Kept for users that rely on this header bringing in command sequencing, output check, buffer and ring pump.
#include "cli_commands.h"
#include "cli_exit_codes.h"
#include "cli_output_check.h"
#include "facet_client.h"
#include "driver_device.h"
#include "raw_capture_buffer.h"
#include "ring_pump.h"
#include "status_line.h"

namespace bitoscope::cli {

struct AcquireSeams : acquisition::AcquisitionSeams {
  std::ostream* err = nullptr;
  std::function<std::optional<uint32_t>()> stderr_columns{};
};

inline int exitCodeFor(const acquisition::AcquireResult& result) {
  return result.memory_shortfall.has_value() ? kExitMemoryFull : kExitComplete;
}

namespace acquire_detail {

std::string describeTriggeredPlan(uint32_t channels, uint64_t sample_rate_milli_hz, uint64_t pre, uint64_t post);

void report(std::ostream& out, const bitoscope::capture::CaptureSummary& summary,
            const bitoscope::capture::CapturePlan& plan, const bitoscope::capture::CaptureSession& session);

void reportShort(std::ostream& out, const bitoscope::capture::CaptureSummary& summary,
                 const bitoscope::capture::CapturePlan& plan, const bitoscope::capture::CaptureSession& session);

}

std::expected<acquisition::AcquireResult, std::string> runAcquire(const CliOptions& opts,
                                                                  const bitoscope::host::pipe::DriverDevice& session,
                                                                  bitoscope::host::FacetTransport& transport,
                                                                  std::ostream& out, AcquireSeams& seams);

}
