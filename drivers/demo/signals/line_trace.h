/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

// WHY: Timing is integer floor(tick * rate / tick_rate): accumulating a rounded samples-per-bit drifts and
// decodes late bytes from the wrong place.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <iterator>
#include <string>
#include <vector>

namespace bitoscope::sim {

using SampleIndex = uint64_t;
using Word = uint32_t;
inline constexpr uint32_t kMaxLines = 32;

struct Edge {
  SampleIndex sample = 0;
  bool level = false;
};

class LineTrace {
 public:
  explicit LineTrace(bool idle_level = false) : idle_(idle_level) {}

  // WHY: set() applies a write that goes back in time at the last edge:
  // an encoder ordering bug gives a late edge, not a crash.
  void set(SampleIndex at, bool level) {
    if (!edges_.empty()) {
      at = std::max(at, edges_.back().sample);
      if (edges_.back().sample == at) {
        edges_.pop_back();
      }
    }
    if (level != finalLevel()) {
      edges_.emplace_back(at, level);
    }
  }

  bool levelAt(SampleIndex at) const {
    const auto it = std::upper_bound(edges_.begin(), edges_.end(), at,
                                     [](SampleIndex v, const Edge& e) { return v < e.sample; });
    return it == edges_.begin() ? idle_ : std::prev(it)->level;
  }

  bool idleLevel() const { return idle_; }
  bool finalLevel() const { return edges_.empty() ? idle_ : edges_.back().level; }
  const std::vector<Edge>& edges() const { return edges_; }

 private:
  bool idle_;
  std::vector<Edge> edges_;
};

class Timebase {
 public:
  static std::expected<Timebase, std::string> create(uint64_t sample_rate_hz,
                                                     uint64_t tick_rate_hz) {
    if (tick_rate_hz == 0) {
      return std::unexpected("tick rate must be non-zero");
    }
    constexpr uint64_t kMaxRate = 4'000'000'000ull;
    if (sample_rate_hz > kMaxRate || tick_rate_hz > kMaxRate) {
      return std::unexpected("rates above 4 GHz are not supported");
    }
    if (sample_rate_hz < 2 * tick_rate_hz) {
      return std::unexpected("sample rate " + std::to_string(sample_rate_hz) +
                              " Hz is too low to represent " + std::to_string(tick_rate_hz) +
                              " Hz signal: at least twice that (" +
                              std::to_string(2 * tick_rate_hz) + " Hz) is needed");
    }
    return Timebase(sample_rate_hz, tick_rate_hz);
  }

  // WHY: The split keeps the intermediate product below 2^64 however large tick gets.
  SampleIndex at(uint64_t tick) const {
    return (tick / tick_rate_) * sample_rate_ + (tick % tick_rate_) * sample_rate_ / tick_rate_;
  }

 private:
  Timebase(uint64_t sample_rate, uint64_t tick_rate)
      : sample_rate_(sample_rate), tick_rate_(tick_rate) {}

  uint64_t sample_rate_;
  uint64_t tick_rate_;
};

inline std::vector<Word> renderLines(const std::vector<LineTrace>& lines, SampleIndex num_samples) {
  std::vector<Word> out(static_cast<size_t>(num_samples), 0);
  const size_t count = std::min<size_t>(lines.size(), kMaxLines);
  for (size_t c = 0; c < count; ++c) {
    const Word bit = Word{1} << c;
    bool level = lines[c].idleLevel();
    SampleIndex pos = 0;
    const auto fill = [&](SampleIndex from, SampleIndex to) {
      if (level) {
        for (SampleIndex s = from; s < to; ++s) {
          out[static_cast<size_t>(s)] |= bit;
        }
      }
    };
    for (const Edge& e : lines[c].edges()) {
      if (e.sample >= num_samples) {
        break;
      }
      fill(pos, e.sample);
      pos = e.sample;
      level = e.level;
    }
    fill(pos, num_samples);
  }
  return out;
}

}
