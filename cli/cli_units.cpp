/*
 * SPDX-License-Identifier: MIT
 */

#include "cli_units.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <limits>
#include <system_error>

#include "capture_plan.h"
#include "triggered_window.h"
#include "trigger_split.h"

namespace bitoscope::cli {

namespace {

namespace units_detail {

constexpr uint64_t kMaxU64 = std::numeric_limits<uint64_t>::max();

char lower(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

bool stripSuffix(std::string_view& text, std::string_view suffix) {
  if (text.size() < suffix.size()) return false;
  const std::string_view tail = text.substr(text.size() - suffix.size());
  for (size_t i = 0; i < suffix.size(); ++i) {
    if (lower(tail[i]) != suffix[i]) return false;
  }
  text.remove_suffix(suffix.size());
  return true;
}

uint64_t stripDecimalSuffix(std::string_view& text) {
  if (text.empty()) return 1;
  uint64_t multiplier = 1;
  switch (lower(text.back())) {
    case 'k': multiplier = 1'000; break;
    case 'm': multiplier = 1'000'000; break;
    case 'g': multiplier = 1'000'000'000; break;
    default: return 1;
  }
  text.remove_suffix(1);
  return multiplier;
}

std::optional<uint64_t> parseDecimal(std::string_view text) {
  if (text.empty()) return std::nullopt;
  for (const char c : text) {
    if (c < '0' || c > '9') return std::nullopt;
  }
  uint64_t value = 0;
  const char* end = text.data() + text.size();
  const auto [ptr, ec] = std::from_chars(text.data(), end, value, 10);
  if (ec != std::errc{} || ptr != end) return std::nullopt;
  return value;
}

std::optional<uint64_t> mulChecked(uint64_t a, uint64_t b) {
  if (a != 0 && b > kMaxU64 / a) return std::nullopt;
  return a * b;
}

std::string badNumber(std::string_view what, std::string_view text) {
  std::string why = std::string(what) + ": '" + std::string(text) + "' is not valid value";
  if (text.find(' ') != std::string_view::npos || text.find('\t') != std::string_view::npos) {
    why += " (no whitespace is allowed inside value)";
  }
  if (text.find('.') != std::string_view::npos) {
    why += " (fractions are not supported; use smaller unit, e.g. 1500k for 1.5m)";
  }
  return why;
}

// WHY: 128-bit product built from 32-bit limbs: MSVC has no __int128.
std::optional<uint64_t> mulDivCeil(uint64_t a, uint64_t b, uint64_t d) {
  constexpr uint64_t kMask = 0xFFFFFFFFull;
  const uint64_t a0 = a & kMask, a1 = a >> 32, b0 = b & kMask, b1 = b >> 32;
  const uint64_t p00 = a0 * b0, p01 = a0 * b1, p10 = a1 * b0, p11 = a1 * b1;
  const uint64_t mid = (p00 >> 32) + (p01 & kMask) + (p10 & kMask);
  const uint64_t hi = p11 + (p01 >> 32) + (p10 >> 32) + (mid >> 32);
  const uint64_t limbs[4] = {hi >> 32, hi & kMask, mid & kMask, p00 & kMask};
  uint64_t quotient[4] = {};
  uint64_t rem = 0;
  for (int i = 0; i < 4; ++i) {
    const uint64_t cur = (rem << 32) | limbs[i];  // WHY: rem < d < 2^32, so shifting by 32 fits in 64 bits.
    quotient[i] = cur / d;
    rem = cur % d;
  }
  if (quotient[0] != 0 || quotient[1] != 0) return std::nullopt;
  uint64_t result = (quotient[2] << 32) | quotient[3];
  if (rem != 0) {
    if (result == kMaxU64) return std::nullopt;
    ++result;
  }
  return result;
}

}

}

// WHY: Suffix m means MEGA, not milli, as in the reference tools.
std::expected<uint64_t, std::string> parseSampleRate(std::string_view text) {
  using namespace units_detail;
  const std::string_view original = text;
  stripSuffix(text, "hz");
  const uint64_t multiplier = stripDecimalSuffix(text);
  const auto number = parseDecimal(text);
  if (!number.has_value()) {
    return std::unexpected(badNumber("samplerate", original));
  }
  if (*number == 0) {
    return std::unexpected("samplerate must be greater than zero");
  }
  constexpr uint64_t kMaxHz = kMaxU64 / 1000;
  const auto hz = mulChecked(*number, multiplier);
  if (!hz.has_value() || *hz > kMaxHz) {
    return std::unexpected("samplerate: " + std::string(original) +
                           " is too large - wire carries millihertz, so at most " +
                           std::to_string(kMaxHz) + " Hz");
  }
  return *hz * 1000;
}

std::expected<uint64_t, std::string> parseSampleCount(std::string_view text) {
  using namespace units_detail;
  const std::string_view original = text;
  const uint64_t multiplier = stripDecimalSuffix(text);
  const auto number = parseDecimal(text);
  if (!number.has_value()) {
    return std::unexpected(badNumber("--samples", original));
  }
  if (*number == 0) {
    return std::unexpected("--samples must be greater than zero");
  }
  const auto samples = mulChecked(*number, multiplier);
  if (!samples.has_value()) {
    return std::unexpected("--samples: " + std::string(original) + " is too large - at most " +
                           std::to_string(kMaxU64) + " samples");
  }
  return *samples;
}

std::expected<uint64_t, std::string> parseDurationMs(std::string_view text) {
  using namespace units_detail;
  const std::string_view original = text;
  uint64_t multiplier = 1;
  if (!stripSuffix(text, "ms") && stripSuffix(text, "s")) {
    multiplier = 1000;
  }
  const auto number = parseDecimal(text);
  if (!number.has_value()) {
    return std::unexpected(badNumber("--time", original));
  }
  if (*number == 0) {
    return std::unexpected("--time must be greater than zero");
  }
  const auto ms = mulChecked(*number, multiplier);
  if (!ms.has_value()) {
    return std::unexpected("--time: " + std::string(original) + " is too large - at most " +
                           std::to_string(kMaxU64 / 1000) + " seconds");
  }
  return *ms;
}

std::expected<uint64_t, std::string> samplesForDuration(uint64_t time_ms, uint64_t rate_milli_hz) {
  if (time_ms == 0 || rate_milli_hz == 0) {
    return std::unexpected("duration and samplerate must both be greater than zero");
  }
  const auto samples = units_detail::mulDivCeil(time_ms, rate_milli_hz, 1'000'000);
  if (!samples.has_value()) {
    return std::unexpected(std::to_string(time_ms) + " ms at " + std::to_string(rate_milli_hz) +
                           " mHz is too many samples to count in 64 bits");
  }
  return *samples;
}

std::expected<uint64_t, std::string> parseSize(std::string_view text) {
  using namespace units_detail;
  const std::string_view original = text;
  uint64_t multiplier = 1;
  if (stripSuffix(text, "kib")) {
    multiplier = uint64_t{1} << 10;
  } else if (stripSuffix(text, "mib")) {
    multiplier = uint64_t{1} << 20;
  } else if (stripSuffix(text, "gib")) {
    multiplier = uint64_t{1} << 30;
  }
  const auto number = parseDecimal(text);
  if (!number.has_value()) {
    return std::unexpected("size: '" + std::string(original) +
                           "' is not whole number of bytes, KiB, MiB or GiB");
  }
  const auto bytes = mulChecked(*number, multiplier);
  if (!bytes.has_value()) {
    return std::unexpected("size: " + std::string(original) + " is too large - at most " +
                           std::to_string(kMaxU64) + " bytes");
  }
  return *bytes;
}

std::expected<ChannelSelection, std::string> parseChannelList(std::string_view text) {
  using namespace units_detail;
  constexpr uint64_t kMaxChannel = 31;
  std::array<std::string, 32> explicit_names;
  uint32_t mask = 0;

  auto number = [&](std::string_view item,
                    std::string_view part) -> std::expected<uint64_t, std::string> {
    const auto value = parseDecimal(part);
    if (!value.has_value()) {
      std::string why = "channel list: '" + std::string(item) + "' is not channel number";
      if (item.starts_with("0x") || item.starts_with("0X")) {
        why += "; bitmask is given with --channel-mask, not as list";
      }
      return std::unexpected(why);
    }
    if (*value > kMaxChannel) {
      return std::unexpected("channel list: channel " + std::string(part) + " is out of range (0-" +
                             std::to_string(kMaxChannel) + ")");
    }
    return *value;
  };

  size_t begin = 0;
  while (true) {
    const size_t comma = text.find(',', begin);
    const std::string_view item =
        text.substr(begin, comma == std::string_view::npos ? std::string_view::npos : comma - begin);
    if (item.empty()) {
      return std::unexpected("channel list: empty item in '" + std::string(text) + "'");
    }

    std::string_view spec = item;
    std::string_view name;
    bool named = false;
    if (const size_t equals = item.find('='); equals != std::string_view::npos) {
      spec = item.substr(0, equals);
      name = item.substr(equals + 1);
      named = true;
      if (name.empty()) {
        return std::unexpected("channel list: '" + std::string(item) + "' has empty name");
      }
      // WHY: All-digit name would read as device index in -P spec, so it could not be addressed.
      if (std::all_of(name.begin(), name.end(), [](char c) { return c >= '0' && c <= '9'; })) {
        return std::unexpected("channel list: name '" + std::string(name) + "' in '" +
                               std::string(item) +
                               "' is made only of digits; digits-only value means device "
                               "index, so give name with letter, such as D" + std::string(name));
      }
      for (const char c : name) {
        if (c == '='|| c == ':' || c == ' ' || c == '\t') {
          return std::unexpected("channel list: name in '" + std::string(item) +
                                 "' may not contain '=', ':' or whitespace");
        }
      }
    }

    uint64_t first = 0;
    uint64_t last = 0;
    if (const size_t dash = spec.find('-'); dash != std::string_view::npos) {
      if (named) {
        return std::unexpected("channel list: '" + std::string(item) +
                               "' names range; name belongs to one channel");
      }
      const auto lo = number(item, spec.substr(0, dash));
      if (!lo.has_value()) return std::unexpected(lo.error());
      const auto hi = number(item, spec.substr(dash + 1));
      if (!hi.has_value()) return std::unexpected(hi.error());
      if (*lo > *hi) {
        return std::unexpected("channel list: range '" + std::string(item) + "' is reversed");
      }
      first = *lo;
      last = *hi;
    } else {
      const auto one = number(item, spec);
      if (!one.has_value()) return std::unexpected(one.error());
      first = last = *one;
    }

    for (uint64_t i = first; i <= last; ++i) {
      mask |= uint32_t{1} << i;
      if (named) explicit_names[i] = std::string(name);
    }

    if (comma == std::string_view::npos) break;
    begin = comma + 1;
  }

  ChannelSelection selection;
  selection.mask = mask;
  for (uint8_t i = 0; i < 32; ++i) {
    if ((mask & (uint32_t{1} << i)) == 0) continue;
    std::string resolved = explicit_names[i].empty() ? "D" + std::to_string(i) : explicit_names[i];
    for (const Channel& earlier : selection.channels) {
      if (earlier.name == resolved) {
        return std::unexpected("channel list: name '" + resolved + "' is given to channels " +
                               std::to_string(earlier.index) + " and " + std::to_string(i));
      }
    }
    selection.channels.push_back({i, std::move(resolved)});
  }
  return selection;
}

std::expected<std::vector<DeviceChannelMatch>, std::string>
parseTriggerSpec(std::string_view text) {
  using namespace units_detail;
  constexpr uint64_t kMaxChannel = 31;
  if (text.empty()) {
    return std::unexpected("--trigger: empty trigger spec");
  }

  std::vector<DeviceChannelMatch> matches;
  size_t begin = 0;
  while (true) {
    const size_t comma = text.find(',', begin);
    const std::string_view item =
        text.substr(begin, comma == std::string_view::npos ? std::string_view::npos : comma - begin);
    if (item.empty()) {
      return std::unexpected("--trigger: empty item in '" + std::string(text) + "'");
    }

    const size_t equals = item.find('=');
    if (equals == std::string_view::npos) {
      return std::unexpected("--trigger: '" + std::string(item) +
                             "' is not CHANNEL=CONDITION");
    }
    const std::string_view channel_text = item.substr(0, equals);
    const std::string_view condition_text = item.substr(equals + 1);
    if (channel_text.empty()) {
      return std::unexpected("--trigger: '" + std::string(item) + "' has no channel before '='");
    }
    if (condition_text.empty()) {
      return std::unexpected("--trigger: '" + std::string(item) + "' has no condition after '='");
    }

    const auto channel = parseDecimal(channel_text);
    if (!channel.has_value()) {
      return std::unexpected("--trigger: '" + std::string(channel_text) +
                             "' is not channel number");
    }
    if (*channel > kMaxChannel) {
      return std::unexpected("--trigger: channel " + std::string(channel_text) +
                             " is out of range (0-" + std::to_string(kMaxChannel) + ")");
    }

    bitoscope::trigger::MatchType type;
    if (condition_text.size() != 1) {
      return std::unexpected("--trigger: '" + std::string(condition_text) +
                             "' is not condition (r, f, e, 0 or 1)");
    }
    switch (condition_text[0]) {
      case 'r': type = bitoscope::trigger::MatchType::kRising; break;
      case 'f': type = bitoscope::trigger::MatchType::kFalling; break;
      case 'e': type = bitoscope::trigger::MatchType::kEdge; break;
      case '0': type = bitoscope::trigger::MatchType::kZero; break;
      case '1': type = bitoscope::trigger::MatchType::kOne; break;
      default:
        return std::unexpected("--trigger: '" + std::string(condition_text) +
                               "' is not condition (r, f, e, 0 or 1)");
    }

    for (const DeviceChannelMatch& earlier : matches) {
      if (earlier.device_channel == *channel) {
        return std::unexpected("--trigger: channel " + std::to_string(*channel) +
                               " is given more than once");
      }
    }
    matches.push_back({static_cast<uint32_t>(*channel), type});

    if (comma == std::string_view::npos) break;
    begin = comma + 1;
  }
  return matches;
}

std::expected<std::vector<bitoscope::trigger::ChannelMatch>, std::string>
mapTriggerChannels(const std::vector<DeviceChannelMatch>& matches,
    const std::vector<Channel>& channels) {
  std::vector<bitoscope::trigger::ChannelMatch> mapped;
  mapped.reserve(matches.size());
  for (const DeviceChannelMatch& match : matches) {
    const auto it = std::find_if(channels.begin(), channels.end(),
                                 [&](const Channel& c) { return c.index == match.device_channel; });
    if (it == channels.end()) {
      std::string selected;
      for (const Channel& c : channels) {
        if (!selected.empty()) selected += ",";
        selected += std::to_string(c.index);
      }
      return std::unexpected("--trigger: channel " + std::to_string(match.device_channel) +
                             " is not in selected channels (" + selected + ")");
    }
    mapped.push_back({static_cast<uint32_t>(it - channels.begin()), match.type});
  }
  return mapped;
}

std::expected<uint32_t, std::string> parseCaptureRatio(std::string_view text) {
  using namespace units_detail;
  const auto value = parseDecimal(text);
  if (!value.has_value() || *value > 100) {
    return std::unexpected("--capture-ratio: '" + std::string(text) +
                           "' is not percentage (0-100)");
  }
  return static_cast<uint32_t>(*value);
}

std::expected<uint64_t, std::string> parsePreSampleCount(std::string_view text) {
  using namespace units_detail;
  const std::string_view original = text;
  const uint64_t multiplier = stripDecimalSuffix(text);
  const auto number = parseDecimal(text);
  if (!number.has_value()) {
    return std::unexpected(badNumber("--pre-samples", original));
  }
  const auto samples = mulChecked(*number, multiplier);
  if (!samples.has_value()) {
    return std::unexpected("--pre-samples: " + std::string(original) + " is too large - at most " +
                           std::to_string(kMaxU64) + " samples");
  }
  return *samples;
}

std::expected<uint64_t, std::string> parsePreDurationMs(std::string_view text) {
  using namespace units_detail;
  const std::string_view original = text;
  uint64_t multiplier = 1;
  if (!stripSuffix(text, "ms") && stripSuffix(text, "s")) {
    multiplier = 1000;
  }
  const auto number = parseDecimal(text);
  if (!number.has_value()) {
    return std::unexpected(badNumber("--pre-time", original));
  }
  const auto ms = mulChecked(*number, multiplier);
  if (!ms.has_value()) {
    return std::unexpected("--pre-time: " + std::string(original) + " is too large - at most " +
                           std::to_string(kMaxU64 / 1000) + " seconds");
  }
  return *ms;
}

// WHY: Single source of window size: runAcquire and validateDecoders must agree, so both take it from here.
uint32_t windowSizeForCapture(bool triggered, uint64_t rate_milli_hz) {
  return triggered ? bitoscope::capture::triggeredWindowSamples(rate_milli_hz)
                   : bitoscope::capture::kSamplesPerWindow;
}

std::expected<bitoscope::trigger::Combine, std::string>
parseTriggerCombine(std::string_view text) {
  using namespace units_detail;
  std::string lowered;
  lowered.reserve(text.size());
  for (const char c : text) lowered.push_back(lower(c));
  if (lowered == "and") return bitoscope::trigger::Combine::kAnd;
  if (lowered == "or") return bitoscope::trigger::Combine::kOr;
  return std::unexpected("--trigger-combine: '" + std::string(text) + "' is not 'and' or 'or'");
}

std::expected<TriggerSizes, std::string> sizeTriggerRange(LengthRequest length,
    PreTriggerRequest pre, uint64_t rate_milli_hz, uint32_t samples_per_window) {
  using units_detail::kMaxU64;
  using units_detail::mulChecked;
  using units_detail::mulDivCeil;

  uint64_t L = 0;
  uint64_t P = 0;

  switch (pre.kind) {
    case PreTriggerKind::kRatio: {
      if (pre.ratio_percent > 100) {
        return std::unexpected("--capture-ratio: " + std::to_string(pre.ratio_percent) +
                               " is not percentage (0-100)");
      }
      L = length.samples;
      const auto product = mulChecked(L, pre.ratio_percent);
      if (!product.has_value()) {
        return std::unexpected(
            "--capture-ratio: capture length times ratio overflows 64-bit total");
      }
      P = bitoscope::trigger::preTriggerSamplesForRatio(L, pre.ratio_percent);
      break;
    }
    case PreTriggerKind::kPreSamples: {
      L = length.samples;
      P = pre.pre_samples;
      if (P > L) {
        return std::unexpected("--pre-samples: " + std::to_string(P) +
                               " is more than capture length, " + std::to_string(L) +
                               " samples");
      }
      break;
    }
    case PreTriggerKind::kPreTime: {
      if (length.time_ms.has_value()) {
        // WHY: Rounding each side up on its own is intended: length may exceed ceil(time * rate) by one sample.
        const uint64_t length_ms = *length.time_ms;
        if (pre.pre_time_ms > length_ms) {
          return std::unexpected("--pre-time: " + std::to_string(pre.pre_time_ms) +
                                 " ms is more than --time, " + std::to_string(length_ms) + " ms");
        }
        const auto pre_samples = mulDivCeil(pre.pre_time_ms, rate_milli_hz, 1'000'000);
        if (!pre_samples.has_value()) {
          return std::unexpected("--pre-time: " + std::to_string(pre.pre_time_ms) + " ms at " +
                                 std::to_string(rate_milli_hz) +
                                 " mHz is too many samples to count in 64 bits");
        }
        const uint64_t remaining_ms = length_ms - pre.pre_time_ms;
        const auto post_samples = mulDivCeil(remaining_ms, rate_milli_hz, 1'000'000);
        if (!post_samples.has_value()) {
          return std::unexpected("--time: " + std::to_string(remaining_ms) + " ms at " +
                                 std::to_string(rate_milli_hz) +
                                 " mHz is too many samples to count in 64 bits");
        }
        P = *pre_samples;
        if (P > kMaxU64 - *post_samples) {
          return std::unexpected(
              "--pre-time/--time: pre-trigger and post-trigger sample counts overflow "
              "64-bit total");
        }
        L = P + *post_samples;
      } else {
        L = length.samples;
        const auto pre_samples = mulDivCeil(pre.pre_time_ms, rate_milli_hz, 1'000'000);
        if (!pre_samples.has_value()) {
          return std::unexpected("--pre-time: " + std::to_string(pre.pre_time_ms) + " ms at " +
                                 std::to_string(rate_milli_hz) +
                                 " mHz is too many samples to count in 64 bits");
        }
        P = *pre_samples;
        if (P > L) {
          return std::unexpected("--pre-time: " + std::to_string(pre.pre_time_ms) + " ms is " +
                                 std::to_string(P) + " samples, more than capture length, " +
                                 std::to_string(L) + " samples");
        }
      }
      break;
    }
  }

  if (L == 0) {
    return std::unexpected("--trigger: capture of zero samples has nothing to trigger in");
  }

  TriggerSizes sizes;
  const bitoscope::trigger::PrePostSplit split = bitoscope::trigger::splitPreTrigger(L, P);
  if (split.pre_was_whole_length) {
    sizes.note = "pre-trigger is " + bitoscope::capture::plan_detail::withThousands(L - 1) +
                " samples, not " + bitoscope::capture::plan_detail::withThousands(L) +
                ", so that file ends on trigger sample";
  }
  sizes.pre = split.pre;
  sizes.post = split.post;
  sizes.length = L;
  if (L > kMaxU64 - samples_per_window) {
    return std::unexpected("--trigger: requested capture length is too long to plan");
  }
  sizes.plan_total_samples = L + samples_per_window;
  return sizes;
}

}
