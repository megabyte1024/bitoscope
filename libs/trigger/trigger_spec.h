/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <expected>
#include <format>
#include <string>
#include <vector>

namespace bitoscope::trigger {

// WHY: Own copy of the project-wide channel cap: this library links neither capture nor decode.
inline constexpr uint32_t kMaxChannels = 32;

enum class MatchType {
  kNone,
  kZero,
  kOne,
  kRising,
  kFalling,
  kEdge,
};

enum class Combine { kAnd, kOr };

struct ChannelMatch {
  uint32_t channel = 0;
  MatchType type = MatchType::kNone;
};

struct TriggerSpec {
  std::vector<ChannelMatch> matches;
  Combine combine = Combine::kAnd;
  uint64_t pre_trigger_samples = 0;
  uint64_t post_trigger_samples = 0;
  uint64_t timeout_s = 0;

  // WHY: A kNone entry is rejected: leaving a channel out of matches is the only way to exclude it.
  std::expected<void, std::string> validate(uint32_t num_channels) const {
    if (matches.empty()) {
      return std::unexpected("trigger spec has no matches: nothing to trigger on");
    }
    for (const ChannelMatch& match : matches) {
      if (match.channel >= num_channels) {
        return std::unexpected(std::format(
            "trigger spec matches channel {}, but capture has {} channel(s)", match.channel,
            num_channels));
      }
      if (match.type == MatchType::kNone) {
        return std::unexpected(std::format(
            "trigger spec has explicit kNone match on channel {}: omit channel from "
            "matches instead of matching it to kNone",
            match.channel));
      }
    }
    return {};
  }
};

}
