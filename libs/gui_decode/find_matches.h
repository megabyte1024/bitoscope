/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <stop_token>
#include <vector>

#include "byte_search.h"

namespace bitoscope::gui_decode {

struct FindMatch {
  std::uint64_t begin = 0;
  std::uint64_t end = 0;
  std::uint32_t decoder = 0;
  std::uint32_t row = 0;
  std::uint32_t laneRank = 0;
  std::uint32_t rowRank = 0;
  bool operator==(const FindMatch&) const = default;
};

struct FindMatchSource {
  std::uint32_t decoder = 0;
  std::uint32_t laneRank = 0;
  std::span<const ByteMatch> matches;
  std::span<const std::uint32_t> rowRanks;
};

struct FindIndexRange {
  std::size_t first = 0;
  std::size_t last = 0;
  bool operator==(const FindIndexRange&) const = default;
};

class FindMatches {
 public:
  static FindMatches build(std::span<const FindMatchSource> sources);
  static std::optional<FindMatches> build(std::span<const FindMatchSource> sources, const std::stop_token& stop);

  std::size_t size() const { return matches_.size(); }
  bool empty() const { return matches_.empty(); }
  const FindMatch& at(std::size_t index) const { return matches_[index]; }

  std::optional<std::size_t> firstFrom(std::uint64_t sample) const;

  std::optional<std::size_t> step(std::size_t current, bool forward) const;

  FindIndexRange candidatesIn(std::uint64_t begin, std::uint64_t end) const;

  static bool overlaps(const FindMatch& match, std::uint64_t begin, std::uint64_t end);

 private:
  std::vector<FindMatch> matches_;
  std::vector<std::uint64_t> runningMaxEnd_;
};

}
