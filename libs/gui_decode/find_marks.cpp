/*
 * SPDX-License-Identifier: MIT
 */

#include "find_marks.h"

#include <algorithm>
#include <optional>

#include "find_lanes.h"

namespace bitoscope::gui_decode {

namespace {

struct OpenRun {
  std::uint64_t begin = 0;
  std::uint64_t end = 0;
  double reach = 0.0;
};

}

std::vector<FindMarkRun> findMarkRuns(const FindMatches& matches, std::uint32_t decoder,
                                      std::span<const bitoscope::decode::AnnotationRow> rows, std::uint64_t begin,
                                      std::uint64_t end, FindMarkScale scale, std::size_t visits) {
  std::vector<FindMarkRun> out;
  std::vector<std::optional<OpenRun>> open(rows.size());
  const FindIndexRange range = matches.candidatesIn(begin, end);
  const std::size_t last = range.last - range.first > visits ? range.first + visits : range.last;
  for (std::size_t i = range.first; i < last; ++i) {
    const FindMatch& match = matches.at(i);
    if (match.decoder != decoder || !FindMatches::overlaps(match, begin, end)) continue;
    const std::optional<std::size_t> slot = shownRowSlot(rows, match.row);
    if (!slot) continue;
    double from = static_cast<double>(match.begin);
    double to = static_cast<double>(match.end);
    if (to - from < scale.shortest) {
      from = (from + to - scale.shortest) * 0.5;
      to = from + scale.shortest;
    }
    std::optional<OpenRun>& run = open[*slot];
    if (run && from <= run->reach + scale.join) {
      run->end = (std::max)(run->end, match.end);
      run->reach = (std::max)(run->reach, to);
      continue;
    }
    if (run) out.push_back({*slot, run->begin, run->end});
    run = OpenRun{match.begin, match.end, to};
  }
  for (std::size_t slot = 0; slot < open.size(); ++slot) {
    if (open[slot]) out.push_back({slot, open[slot]->begin, open[slot]->end});
  }
  return out;
}

}
