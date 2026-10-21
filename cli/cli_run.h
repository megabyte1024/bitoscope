/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <iosfwd>
#include <optional>
#include <span>
#include <string_view>

#include "ring_config.h"

namespace bitoscope::capture {
class CaptureWriter;
class MemoryProbe;
}

namespace bitoscope::acquisition {
struct AcquireResult;
}

namespace bitoscope::cli {

struct CliEnvironment {
  std::ostream* out = nullptr;
  std::ostream* err = nullptr;
  std::filesystem::path executable;
  std::function<bool()> interrupted;
  std::function<std::optional<uint32_t>()> stderr_columns;

  bitoscope::capture::CaptureWriter* writer_for_test = nullptr;
  bitoscope::capture::MemoryProbe* probe_for_test = nullptr;
  std::optional<uint64_t> inactivity_timeout_ms_for_test;
  std::optional<acquisition::RingGeometry> ring_geometry_for_test;
  std::function<void(const acquisition::AcquireResult&)> on_result_for_test;
  std::function<void(const std::atomic<bool>& stop)> before_decode_feed_for_test;
  std::optional<uint64_t> decode_ring_limit_for_test;
  std::function<void(uint64_t collected)> before_annotation_collect_for_test;
  std::optional<uint64_t> list_again_interval_ms_for_test;
};

int runCli(std::span<const std::string_view> args, const CliEnvironment& environment);

}
