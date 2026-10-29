/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "trigger_spec.h"

namespace bitoscope::cli {

struct Channel {
  uint8_t index = 0;
  std::string name;
};

struct ChannelSelection {
  uint32_t mask = 0;
  std::vector<Channel> channels;
};

std::expected<uint64_t, std::string> parseSampleRate(std::string_view text);

std::expected<uint64_t, std::string> parseSampleCount(std::string_view text);

std::expected<uint64_t, std::string> parseDurationMs(std::string_view text);

std::expected<uint64_t, std::string> samplesForDuration(uint64_t time_ms, uint64_t rate_milli_hz);

std::expected<uint64_t, std::string> parseSize(std::string_view text);

std::expected<ChannelSelection, std::string> parseChannelList(std::string_view text);

// WHY: Separate type from trigger::ChannelMatch: device channel and capture index were confused three times.
struct DeviceChannelMatch {
  uint32_t device_channel = 0;
  bitoscope::trigger::MatchType type = bitoscope::trigger::MatchType::kNone;
};

std::expected<std::vector<DeviceChannelMatch>, std::string>
parseTriggerSpec(std::string_view text);

std::expected<std::vector<bitoscope::trigger::ChannelMatch>, std::string>
mapTriggerChannels(const std::vector<DeviceChannelMatch>& matches,
    const std::vector<Channel>& channels);

std::expected<uint32_t, std::string> parseCaptureRatio(std::string_view text);

std::expected<uint64_t, std::string> parsePreSampleCount(std::string_view text);

std::expected<uint64_t, std::string> parsePreDurationMs(std::string_view text);

uint32_t windowSizeForCapture(bool triggered, uint64_t rate_milli_hz);

std::expected<bitoscope::trigger::Combine, std::string>
parseTriggerCombine(std::string_view text);

// WHY: Plan carries one extra window beyond pre + post: room for front offset of window grid, never saved.
struct TriggerSizes {
  uint64_t pre = 0;
  uint64_t post = 0;
  uint64_t length = 0;
  uint64_t plan_total_samples = 0;
  std::optional<std::string> note;
};

enum class PreTriggerKind { kRatio, kPreSamples, kPreTime };

struct PreTriggerRequest {
  PreTriggerKind kind = PreTriggerKind::kRatio;
  uint32_t ratio_percent = 0;
  uint64_t pre_samples = 0;
  uint64_t pre_time_ms = 0;
};

struct LengthRequest {
  uint64_t samples = 0;
  std::optional<uint64_t> time_ms;
};

std::expected<TriggerSizes, std::string> sizeTriggerRange(LengthRequest length,
    PreTriggerRequest pre, uint64_t rate_milli_hz, uint32_t samples_per_window);

}
