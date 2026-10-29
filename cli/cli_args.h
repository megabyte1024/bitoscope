/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <chrono>
#include <cstdint>
#include <expected>
#include <limits>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "cli_units.h"
#include "ring_config.h"
#include "trigger_spec.h"

namespace bitoscope::cli {

enum class Action {
  Listen,
  Help,
  StartCapture,
  Stop,
  PwmStart,
  PwmStop,
  Acquire,
  ListDevices,
  WatchDevices,
  ListProperties,
  CheckDriver
};

enum class MemoryStrategy { Chunked, Whole };

enum class OnMemoryFull { Keep, Discard };

// WHY: device_matches and capture_matches differ in type on purpose: device numbers must not act as capture indices.
struct TriggerOptions {
  std::vector<DeviceChannelMatch> device_matches;
  std::vector<bitoscope::trigger::ChannelMatch> capture_matches;
  bitoscope::trigger::Combine combine = bitoscope::trigger::Combine::kAnd;
  uint32_t capture_ratio_percent = 0;
  uint64_t timeout_s = 0;
  std::optional<uint64_t> pre_samples;
  std::optional<uint64_t> pre_time_ms;
};

PreTriggerRequest preTriggerRequest(const TriggerOptions& trigger);

struct CliOptions {
  Action action = Action::Listen;
  std::string plugin;
  std::string device;
  std::string plugin_dir;
  std::string check_driver;
  std::chrono::seconds wait{30};

  uint64_t rate_hz = 0;
  uint32_t channel_mask = 0;
  uint64_t sample_limit = 0;
  bool buffered = false;
  std::chrono::seconds duration{0};
  std::map<std::string, std::string> configure;

  uint64_t sample_rate_milli_hz = 0;
  uint64_t total_samples = 0;
  std::optional<uint64_t> time_ms;
  std::vector<Channel> channels;
  std::string output_file;
  std::string output_format = "hdf5";
  std::optional<uint32_t> gzip_level;
  std::vector<std::string> decoders;
  std::vector<std::string> annotation_classes;
  std::string decoder_plugin;
  std::string decoder_dir;
  std::optional<std::string> decoder_script_dir;
  MemoryStrategy memory = MemoryStrategy::Chunked;
  std::optional<uint64_t> reserve_bytes;
  std::optional<uint64_t> memory_budget_bytes;
  uint32_t start_retries = 2;
  OnMemoryFull on_memory_full = OnMemoryFull::Keep;
  std::optional<TriggerOptions> trigger;
  uint64_t ring_mib = acquisition::kDefaultRingMiB;
  uint32_t pack_workers = 0;
  uint32_t clearing_workers = 1;
  bool quiet = false;

  uint32_t port_mask = 0;
  uint64_t freq_hz = 0;
  uint32_t duty_percent = 50;
};

inline uint32_t windowSizeForCapture(const CliOptions& opts) {
  return windowSizeForCapture(opts.trigger.has_value(), opts.sample_rate_milli_hz);
}

std::string_view usageText();

std::expected<CliOptions, std::string> parseArgs(std::span<const std::string_view> args,
    uint32_t hardware_threads = std::thread::hardware_concurrency());

namespace detail {

inline constexpr uint64_t kMaxWholeHz = std::numeric_limits<uint64_t>::max() / 1000;

}

}
