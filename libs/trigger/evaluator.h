/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <cstring>
#include <expected>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <utility>

#if defined(_MSC_VER) && defined(_M_X64)
#include <immintrin.h>
#endif

#include "fold_steps.h"
#include "sample_window.h"
#include "trigger_spec.h"

namespace bitoscope::trigger {

namespace evaluator_detail {

inline constexpr uint32_t kStepSamples = 64;
inline constexpr uint32_t kStepBytes = kStepSamples / 8;
inline constexpr uint32_t kBlockSteps = 32;
inline constexpr uint64_t kAll = ~uint64_t{0};
inline constexpr uint64_t kEarliest = uint64_t{1};

// WHY: TrailingZeros needs a non-zero step: __builtin_ctzll(0) is undefined and BSF leaves 0 undefined.
inline unsigned TrailingZeros(uint64_t step) {
#if defined(_MSC_VER) && defined(_M_X64)
  return static_cast<unsigned>(_tzcnt_u64(step));
#else
  return static_cast<unsigned>(__builtin_ctzll(step));
#endif
}

inline uint64_t LoadStep(const uint8_t* bytes, uint32_t count) {
  uint64_t step = 0;
  if (count == kStepBytes) {
    std::memcpy(&step, bytes, kStepBytes);
  } else {
    std::memcpy(&step, bytes, count);
  }
  if constexpr (std::endian::native == std::endian::big) {
    step = std::byteswap(step);
  }
  return step;
}

inline StepTest testOf(MatchType type) {
  switch (type) {
    case MatchType::kZero:
      return StepTest::kLow;
    case MatchType::kOne:
      return StepTest::kHigh;
    case MatchType::kRising:
      return StepTest::kRising;
    case MatchType::kFalling:
      return StepTest::kFalling;
    case MatchType::kEdge:
    case MatchType::kNone:
      break;
  }
  return StepTest::kEither;
}

}

class Evaluator {
 public:
  static std::expected<Evaluator, std::string> create(TriggerSpec spec, uint32_t num_channels) {
    if (auto ok = spec.validate(num_channels); !ok) {
      return std::unexpected(std::move(ok.error()));
    }
    if (num_channels > kMaxChannels) {
      return std::unexpected(std::format(
          "trigger evaluator supports at most {} channels, got {}", kMaxChannels, num_channels));
    }
    Evaluator eval;
    eval.spec_ = std::move(spec);
    return eval;
  }

  std::optional<uint64_t> scan(const bitoscope::host::WindowLease& window) {
    using evaluator_detail::kStepBytes;

    const uint32_t num_samples = window.info().num_samples;
    if (num_samples == 0) return std::nullopt;
    // WHY: Bound the loop by ceil(num_samples / 8), not pool width: bytes past num_samples underflow valid_bits.
    const uint32_t bytes_per_channel = (num_samples + 7u) / 8u;
    const uint32_t full_steps = bytes_per_channel / kStepBytes;
    const uint32_t tail_bytes = bytes_per_channel % kStepBytes;
    const uint32_t steps = full_steps + (tail_bytes != 0 ? 1u : 0u);

    // WHY: Matches below pre_trigger_samples are masked, not skipped: last_bit_ must advance through them.
    // An early return would compare the first armed sample against a stale predecessor.
    Window w{.window = window, .first = window.info().first_sample_index, .num_samples = num_samples};
    if (spec_.pre_trigger_samples > w.first) {
      const uint64_t relative = spec_.pre_trigger_samples - w.first;
      w.head_suppressed = (relative >= num_samples) ? num_samples : static_cast<uint32_t>(relative);
    }

    // WHY: Edge at first sample ever scanned is suppressed: zero-initialised last_bit_ fakes a rising edge there.
    // Level matches must still fire, so this stays apart from the head mask.
    const bool first_window = !scanned_any_;
    scanned_any_ = true;

    uint32_t step = 0;
    if (first_window) {
      const uint32_t step_bytes = (full_steps > 0) ? kStepBytes : tail_bytes;
      const uint64_t combined = combineOneStep(window, 0, step_bytes, ~evaluator_detail::kEarliest);
      if (const std::optional<uint64_t> hit = hitIn(w, 0, combined)) {
        settleCarries(window, 0, step_bytes);
        return hit;
      }
      step = 1;
    }

    const StepCombine combine = (spec_.combine == Combine::kAnd) ? StepCombine::kAnd : StepCombine::kOr;
    std::array<uint64_t, evaluator_detail::kBlockSteps> combined{};
    while (step < full_steps) {
      const uint32_t count = (std::min)(evaluator_detail::kBlockSteps, full_steps - step);
      const std::span<uint64_t> block(combined.data(), count);
      std::ranges::fill(block, (combine == StepCombine::kAnd) ? evaluator_detail::kAll : 0);
      for (const ChannelMatch& match : spec_.matches) {
        const std::span<const uint8_t> bytes = window.channel(match.channel);
        const bool sample_before = sampleBefore(bytes, match.channel, step);
        const uint64_t any = foldWholeSteps(evaluator_detail::testOf(match.type), combine,
                                            bytes.subspan(static_cast<size_t>(step) * kStepBytes,
                                                          static_cast<size_t>(count) * kStepBytes),
                                            sample_before, block);
        // WHY: Stopping an AND block early is safe only because each block reads its edge carry from the data.
        if (combine == StepCombine::kAnd && any == 0) break;
      }
      for (uint32_t k = 0; k < count; ++k) {
        if (block[k] == 0) continue;
        if (const std::optional<uint64_t> hit = hitIn(w, step + k, block[k])) {
          settleCarries(window, step + k, kStepBytes);
          return hit;
        }
      }
      step += count;
    }

    if (step < steps) {
      const uint64_t tail = combineOneStep(window, step, tail_bytes, evaluator_detail::kAll);
      if (const std::optional<uint64_t> hit = hitIn(w, step, tail)) {
        settleCarries(window, step, tail_bytes);
        return hit;
      }
    }
    const uint32_t last_step = steps - 1;
    settleCarries(window, last_step, (last_step < full_steps) ? kStepBytes : tail_bytes);
    return std::nullopt;
  }

 private:
  struct Window {
    const bitoscope::host::WindowLease& window;
    uint64_t first = 0;
    uint32_t num_samples = 0;
    uint32_t head_suppressed = 0;
  };

  static std::optional<uint64_t> hitIn(const Window& w, uint32_t step, uint64_t combined) {
    using evaluator_detail::kAll;
    using evaluator_detail::kStepSamples;
    const uint64_t samples_before_step = static_cast<uint64_t>(step) * kStepSamples;
    if (w.head_suppressed > samples_before_step) {
      const uint64_t suppressed_here = w.head_suppressed - samples_before_step;
      combined &= (suppressed_here >= kStepSamples) ? 0 : (kAll << suppressed_here);
    }
    // WHY: Mask padding bits past num_samples: a kZero match would otherwise fire on zero padding.
    const uint64_t valid_bits = w.num_samples - samples_before_step;
    if (valid_bits < kStepSamples) {
      combined &= kAll >> (kStepSamples - valid_bits);
    }
    if (combined == 0) return std::nullopt;
    return w.first + samples_before_step + evaluator_detail::TrailingZeros(combined);
  }

  bool sampleBefore(std::span<const uint8_t> bytes, uint32_t channel, uint32_t step) const {
    if (step == 0) return last_bit_[channel];
    return (bytes[static_cast<size_t>(step) * evaluator_detail::kStepBytes - 1] >> 7) != 0;
  }

  uint64_t combineOneStep(const bitoscope::host::WindowLease& window, uint32_t step, uint32_t step_bytes,
                          uint64_t edge_mask) const {
    using evaluator_detail::kEarliest;
    uint64_t combined = (spec_.combine == Combine::kAnd) ? evaluator_detail::kAll : 0;
    for (const ChannelMatch& match : spec_.matches) {
      const std::span<const uint8_t> bytes = window.channel(match.channel);
      const uint64_t word =
          evaluator_detail::LoadStep(bytes.data() + static_cast<size_t>(step) * evaluator_detail::kStepBytes,
                                     step_bytes);
      uint64_t contribution = 0;
      switch (match.type) {
        case MatchType::kZero:
          contribution = ~word;
          break;
        case MatchType::kOne:
          contribution = word;
          break;
        case MatchType::kRising:
        case MatchType::kFalling:
        case MatchType::kEdge: {
          const uint64_t carry = sampleBefore(bytes, match.channel, step) ? kEarliest : 0;
          const uint64_t edge = word ^ ((word << 1) | carry);
          if (match.type == MatchType::kRising) {
            contribution = edge & word;
          } else if (match.type == MatchType::kFalling) {
            contribution = edge & ~word;
          } else {
            contribution = edge;
          }
          contribution &= edge_mask;
          break;
        }
        case MatchType::kNone:
          break;
      }
      combined = (spec_.combine == Combine::kAnd) ? (combined & contribution) : (combined | contribution);
    }
    return combined;
  }

  void settleCarries(const bitoscope::host::WindowLease& window, uint32_t step, uint32_t step_bytes) {
    for (const ChannelMatch& match : spec_.matches) {
      if (match.type == MatchType::kZero || match.type == MatchType::kOne) continue;
      const std::span<const uint8_t> bytes = window.channel(match.channel);
      const size_t last = static_cast<size_t>(step) * evaluator_detail::kStepBytes + step_bytes - 1;
      // WHY: Set the carry even when masks hid every match: suppressed samples still count as predecessors.
      last_bit_[match.channel] = (bytes[last] >> 7) != 0;
    }
  }

  TriggerSpec spec_;
  std::array<bool, kMaxChannels> last_bit_{};
  bool scanned_any_ = false;
};

}
