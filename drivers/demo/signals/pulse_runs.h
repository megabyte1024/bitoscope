/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "line_trace.h"

namespace bitoscope::sim {

struct Run {
  bool level = false;
  uint64_t length = 0;
  bool operator==(const Run&) const = default;
};
using Runs = std::vector<Run>;

struct PulseDifference {
  uint32_t channel = 0;
  uint64_t pulse = 0;
  bool level = false;
  uint64_t expected_length = 0;
  uint64_t actual_length = 0;

  std::string describe() const {
    return "channel " + std::to_string(channel) + " pulse " + std::to_string(pulse) + " (" +
           (level ? "high" : "low") + "): expected " + std::to_string(expected_length) +
           " samples, got " + std::to_string(actual_length);
  }
};

inline std::expected<Runs, std::string> runsFromLengths(bool first_level,
                                                        std::span<const uint64_t> lengths) {
  Runs out;
  out.reserve(lengths.size());
  bool level = first_level;
  for (size_t i = 0; i < lengths.size(); ++i) {
    if (lengths[i] == 0) {
      return std::unexpected("pulse " + std::to_string(i) + " has length 0");
    }
    out.push_back(Run{level, lengths[i]});
    level = !level;
  }
  return out;
}

inline uint64_t totalLength(const Runs& runs) {
  uint64_t total = 0;
  for (const Run& r : runs) {
    total += r.length;
  }
  return total;
}

inline LineTrace traceFromRuns(const Runs& runs) {
  LineTrace trace(runs.empty() ? false : runs.front().level);
  SampleIndex at = 0;
  for (const Run& r : runs) {
    trace.set(at, r.level);
    at += r.length;
  }
  return trace;
}

namespace detail {

inline void appendSample(Runs& runs, bool level) {
  if (!runs.empty() && runs.back().level == level) {
    ++runs.back().length;
  } else {
    runs.push_back(Run{level, 1});
  }
}

}

inline Runs runsOfWords(std::span<const Word> samples, uint32_t channel) {
  Runs runs;
  for (const Word w : samples) {
    detail::appendSample(runs, ((w >> channel) & 1u) != 0);
  }
  return runs;
}

inline std::expected<Runs, std::string> runsOfChannelBytes(std::span<const uint8_t> bytes,
                                                           uint64_t num_samples) {
  if (bytes.size() < (num_samples + 7) / 8) {
    return std::unexpected(std::to_string(bytes.size()) + " bytes cannot hold " +
                           std::to_string(num_samples) + " samples");
  }
  Runs runs;
  for (uint64_t s = 0; s < num_samples; ++s) {
    detail::appendSample(runs, ((bytes[static_cast<size_t>(s / 8)] >> (s % 8)) & 1u) != 0);
  }
  return runs;
}

inline std::optional<PulseDifference> firstDifference(uint32_t channel, const Runs& expected,
                                                      const Runs& actual) {
  const size_t count = expected.size() > actual.size() ? expected.size() : actual.size();
  for (size_t i = 0; i < count; ++i) {
    const bool have_e = i < expected.size();
    const bool have_a = i < actual.size();
    if (have_e && have_a && expected[i] == actual[i]) {
      continue;
    }
    PulseDifference d;
    d.channel = channel;
    d.pulse = i;
    d.level = have_e ? expected[i].level : actual[i].level;
    d.expected_length = have_e ? expected[i].length : 0;
    d.actual_length = have_a && actual[i].level == d.level ? actual[i].length : 0;
    return d;
  }
  return std::nullopt;
}

}
