/*
 * SPDX-License-Identifier: MIT
 */

#include "find_matches.h"

#include <algorithm>
#include <tuple>

namespace bitoscope::gui_decode {

namespace {

constexpr std::size_t kMatchesBetweenStopChecks = std::size_t{1} << 16;
constexpr std::size_t kRunsPickedInTurn = 8;

std::uint32_t rankOfRow(std::span<const std::uint32_t> rowRanks, std::uint32_t row) {
  if (row < rowRanks.size()) return rowRanks[row];
  return static_cast<std::uint32_t>(rowRanks.size()) + row;
}

FindMatch matchOf(const FindMatchSource& source, std::size_t i) {
  const ByteMatch& m = source.matches[i];
  return {m.begin, m.end, source.decoder, m.row, source.laneRank, rankOfRow(source.rowRanks, m.row)};
}

bool before(const FindMatch& a, const FindMatch& b) {
  return std::tie(a.begin, a.laneRank, a.rowRank) < std::tie(b.begin, b.laneRank, b.rowRank);
}

struct Run {
  const FindMatchSource* source = nullptr;
  std::size_t next = 0;
  std::size_t end = 0;
  FindMatch head;
};

std::vector<Run> ascendingRuns(std::span<const FindMatchSource> sources) {
  std::vector<Run> runs;
  for (const FindMatchSource& source : sources) {
    std::uint64_t lastBegin = 0;
    std::uint32_t lastRank = 0;
    for (std::size_t i = 0; i < source.matches.size(); ++i) {
      const std::uint64_t begin = source.matches[i].begin;
      const std::uint32_t rank = rankOfRow(source.rowRanks, source.matches[i].row);
      if (i == 0 || std::tie(begin, rank) < std::tie(lastBegin, lastRank)) {
        if (i > 0) runs.back().end = i;
        runs.push_back({&source, i, source.matches.size(), matchOf(source, i)});
      }
      lastBegin = begin;
      lastRank = rank;
    }
  }
  return runs;
}

}

FindMatches FindMatches::build(std::span<const FindMatchSource> sources) { return *build(sources, std::stop_token{}); }

std::optional<FindMatches> FindMatches::build(std::span<const FindMatchSource> sources, const std::stop_token& stop) {
  std::vector<Run> runs = ascendingRuns(sources);
  std::size_t total = 0;
  for (const FindMatchSource& source : sources) total += source.matches.size();
  FindMatches out;
  out.matches_.reserve(total);
  out.runningMaxEnd_.reserve(total);
  std::uint64_t maxEnd = 0;
  const auto take = [&](Run& run) {
    out.matches_.push_back(run.head);
    maxEnd = (std::max)(maxEnd, run.head.end);
    out.runningMaxEnd_.push_back(maxEnd);
    if (++run.next == run.end) return false;
    run.head = matchOf(*run.source, run.next);
    return true;
  };
  const auto later = [](const Run& a, const Run& b) { return before(b.head, a.head); };
  const bool inTurn = runs.size() <= kRunsPickedInTurn;
  if (!inTurn) std::ranges::make_heap(runs, later);
  for (std::size_t merged = 1; !runs.empty(); ++merged) {
    if (merged % kMatchesBetweenStopChecks == 0 && stop.stop_requested()) return std::nullopt;
    if (inTurn) {
      std::size_t pick = 0;
      for (std::size_t r = 1; r < runs.size(); ++r) {
        if (before(runs[r].head, runs[pick].head)) pick = r;
      }
      if (!take(runs[pick])) runs.erase(runs.begin() + static_cast<std::ptrdiff_t>(pick));
    } else {
      std::ranges::pop_heap(runs, later);
      if (take(runs.back())) {
        std::ranges::push_heap(runs, later);
      } else {
        runs.pop_back();
      }
    }
  }
  return out;
}

std::optional<std::size_t> FindMatches::firstFrom(std::uint64_t sample) const {
  if (matches_.empty()) return std::nullopt;
  const auto it = std::ranges::lower_bound(matches_, sample, {}, &FindMatch::begin);
  if (it == matches_.end()) return 0;
  return static_cast<std::size_t>(it - matches_.begin());
}

std::optional<std::size_t> FindMatches::step(std::size_t current, bool forward) const {
  if (matches_.empty()) return std::nullopt;
  if (forward) return current + 1 >= matches_.size() ? 0 : current + 1;
  return current == 0 || current > matches_.size() ? matches_.size() - 1 : current - 1;
}

FindIndexRange FindMatches::candidatesIn(std::uint64_t begin, std::uint64_t end) const {
  const auto first = std::ranges::upper_bound(runningMaxEnd_, begin);
  const auto last = std::ranges::lower_bound(matches_, end, {}, &FindMatch::begin);
  const auto firstIndex = static_cast<std::size_t>(first - runningMaxEnd_.begin());
  const auto lastIndex = static_cast<std::size_t>(last - matches_.begin());
  return {firstIndex, (std::max)(firstIndex, lastIndex)};
}

bool FindMatches::overlaps(const FindMatch& match, std::uint64_t begin, std::uint64_t end) {
  return match.begin < end && match.end > begin;
}

}
