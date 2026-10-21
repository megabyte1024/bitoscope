/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <expected>
#include <format>
#include <limits>
#include <string>
#include <string_view>

namespace bitoscope::capture {

inline constexpr uint32_t kSamplesPerWindow = 1'048'576;
inline constexpr uint32_t kMaxChannels = 32;

namespace plan_detail {

inline constexpr uint64_t kMaxU64 = std::numeric_limits<uint64_t>::max();

// WHY: Apostrophe, not comma, as thousands separator: in many countries the comma is the decimal separator.
inline std::string withThousands(uint64_t value) {
  std::string digits = std::to_string(value);
  std::string out;
  for (size_t i = 0; i < digits.size(); ++i) {
    if (i != 0 && (digits.size() - i) % 3 == 0) out += '\'';
    out += digits[i];
  }
  return out;
}

inline std::string formatRate(uint64_t milli_hz) {
  struct Unit {
    uint64_t milli_hz;
    std::string_view name;
  };
  static constexpr Unit kUnits[] = {
      {1'000'000'000'000ULL, "GHz"}, {1'000'000'000ULL, "MHz"}, {1'000'000ULL, "kHz"},
      {1'000ULL, "Hz"}};
  for (const Unit& unit : kUnits) {
    if (milli_hz % unit.milli_hz == 0) {
      return std::format("{} {}", milli_hz / unit.milli_hz, unit.name);
    }
  }
  return std::format("{} mHz", milli_hz);
}

inline std::string formatDuration(uint64_t samples, uint64_t milli_hz) {
  const double seconds = static_cast<double>(samples) * 1000.0 / static_cast<double>(milli_hz);
  if (seconds >= 1.0) return std::format("{:g} s", seconds);
  if (seconds >= 1e-3) return std::format("{:g} ms", seconds * 1e3);
  return std::format("{:g} us", seconds * 1e6);
}

inline std::string formatSize(uint64_t bytes) {
  if (bytes == 1) return "1 byte";
  if (bytes < 1024) return std::format("{} bytes", bytes);
  struct Unit {
    uint64_t bytes;
    std::string_view name;
  };
  static constexpr Unit kUnits[] = {{1ULL << 40, "TiB"}, {1ULL << 30, "GiB"},
                                    {1ULL << 20, "MiB"}, {1ULL << 10, "KiB"}};
  for (const Unit& unit : kUnits) {
    if (bytes < unit.bytes && unit.bytes != (1ULL << 10)) continue;
    uint64_t whole = bytes / unit.bytes;
    const uint64_t remainder = bytes % unit.bytes;
    uint64_t tenths = (remainder * 10 + unit.bytes / 2) / unit.bytes;
    if (tenths == 10) {
      ++whole;
      tenths = 0;
    }
    return std::format("{}.{} {}", whole, tenths, unit.name);
  }
  return std::format("{} bytes", bytes);
}

}

class CapturePlan {
 public:
  uint32_t channels() const { return channels_; }
  uint64_t sampleRateMilliHz() const { return sample_rate_milli_hz_; }
  uint64_t totalSamples() const { return total_samples_; }
  uint32_t samplesPerWindow() const { return samples_per_window_; }

  uint64_t bytesPerChannel() const { return bytes_per_channel_; }
  uint64_t totalBytes() const { return total_bytes_; }
  uint64_t slots() const { return slots_; }

  uint64_t slotSamples(uint64_t index) const {
    if (index + 1 < slots_) return samples_per_window_;
    return total_samples_ - (slots_ - 1) * samples_per_window_;
  }

  uint64_t slotBytes(uint64_t index) const {
    return static_cast<uint64_t>(channels_) * ((slotSamples(index) + 7) / 8);
  }

  std::string describe() const {
    return std::format("{} {}, {}, {} = {} samples, {} uncompressed", channels_,
                       channels_ == 1 ? "channel" : "channels",
                       plan_detail::formatRate(sample_rate_milli_hz_),
                       plan_detail::formatDuration(total_samples_, sample_rate_milli_hz_),
                       plan_detail::withThousands(total_samples_),
                       plan_detail::formatSize(total_bytes_));
  }

 private:
  friend std::expected<CapturePlan, std::string> makeCapturePlan(uint32_t, uint64_t, uint64_t,
                                                                 uint32_t);

  uint32_t channels_ = 0;
  uint64_t sample_rate_milli_hz_ = 0;
  uint64_t total_samples_ = 0;
  uint32_t samples_per_window_ = 0;
  uint64_t bytes_per_channel_ = 0;
  uint64_t total_bytes_ = 0;
  uint64_t slots_ = 0;
};

inline std::expected<CapturePlan, std::string> makeCapturePlan(
    uint32_t channels, uint64_t sample_rate_milli_hz, uint64_t total_samples,
    uint32_t samples_per_window = kSamplesPerWindow) {
  if (channels == 0 || channels > kMaxChannels) {
    return std::unexpected(std::format("channel count must be 1 to {}, got {}", kMaxChannels,
                                       channels));
  }
  if (sample_rate_milli_hz == 0) return std::unexpected("sample rate must be positive");
  if (total_samples == 0) return std::unexpected("capture length must be at least one sample");
  if (samples_per_window == 0 || samples_per_window % 8 != 0) {
    return std::unexpected(std::format(
        "samples per window must be positive multiple of 8, got {}", samples_per_window));
  }

  CapturePlan plan;
  plan.channels_ = channels;
  plan.sample_rate_milli_hz_ = sample_rate_milli_hz;
  plan.total_samples_ = total_samples;
  plan.samples_per_window_ = samples_per_window;
  plan.bytes_per_channel_ = total_samples / 8 + (total_samples % 8 != 0 ? 1 : 0);
  if (plan.bytes_per_channel_ > plan_detail::kMaxU64 / channels) {
    return std::unexpected(std::format(
        "{} channels x {} samples needs {} bytes per channel, which overflows 64-bit byte "
        "count",
        channels, total_samples, plan.bytes_per_channel_));
  }
  plan.total_bytes_ = plan.bytes_per_channel_ * channels;
  plan.slots_ = total_samples / samples_per_window + (total_samples % samples_per_window != 0 ? 1 : 0);
  return plan;
}

inline uint64_t longestSamples(uint64_t budget_bytes, uint32_t channels) {
  if (channels == 0) return 0;
  const uint64_t bytes_per_channel = budget_bytes / channels;
  if (bytes_per_channel > plan_detail::kMaxU64 / 8) return plan_detail::kMaxU64;
  return bytes_per_channel * 8;
}

constexpr uint64_t packPoolDepth(uint32_t pack_workers) { return uint64_t{pack_workers} * 2; }

// WHY: One function for both callers (CaptureSession::create and cli_acquire): they must not derive the pool
// size independently. It lives here because capture_session.h pulls in the window assembler.
inline uint64_t windowPoolBuffers(uint32_t pack_workers) {
  return 2 + packPoolDepth(pack_workers);
}

inline uint64_t windowPoolBytes(uint64_t num_buffers, uint32_t channels,
                                uint32_t samples_per_window) {
  const uint64_t bytes_per_channel = (static_cast<uint64_t>(samples_per_window) + 7) / 8;
  if (channels != 0 && bytes_per_channel > plan_detail::kMaxU64 / channels) {
    return plan_detail::kMaxU64;
  }
  const uint64_t per_buffer = bytes_per_channel * channels;
  if (num_buffers != 0 && per_buffer > plan_detail::kMaxU64 / num_buffers) {
    return plan_detail::kMaxU64;
  }
  return per_buffer * num_buffers;
}

enum class TriggeredMemoryAnswer : uint8_t { kRun, kWarn, kRefuse };

struct TriggeredMemoryFit {
  TriggeredMemoryAnswer answer = TriggeredMemoryAnswer::kRefuse;
  uint64_t needed_samples = 0;
  uint64_t usable_samples = 0;
  uint64_t post_floor = 0;
};

constexpr TriggeredMemoryFit triggeredMemoryFit(uint64_t samples_floor, uint64_t pre,
                                                uint64_t post, uint32_t samples_per_window) {
  const uint64_t w = samples_per_window == 0 ? 1 : samples_per_window;
  const auto add = [](uint64_t a, uint64_t b) {
    return a > plan_detail::kMaxU64 - b ? plan_detail::kMaxU64 : a + b;
  };
  const uint64_t plan_total = add(add(pre, post), w);
  const uint64_t worst_end = plan_total - 1;
  const uint64_t usable =
      samples_floor >= plan_total ? plan_total : samples_floor / w * w;

  const uint64_t drain_windows = add(pre / w + (pre % w != 0 ? 1 : 0), 1);
  const uint64_t drain =
      drain_windows > plan_detail::kMaxU64 / w ? plan_detail::kMaxU64 : drain_windows * w;

  TriggeredMemoryFit fit;
  fit.usable_samples = usable;
  fit.needed_samples = drain < worst_end ? drain : worst_end;
  if (usable >= worst_end) {
    fit.answer = TriggeredMemoryAnswer::kRun;
    fit.post_floor = post;
  } else if (usable < fit.needed_samples) {
    fit.answer = TriggeredMemoryAnswer::kRefuse;
  } else {
    fit.answer = TriggeredMemoryAnswer::kWarn;
    fit.post_floor = usable - pre - (w - 1);
  }
  return fit;
}

}
