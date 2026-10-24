/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "decoder.h"
#include "find_matches.h"

namespace bitoscope::gui_decode {

struct FindMarkRun {
  std::size_t slot = 0;
  std::uint64_t begin = 0;
  std::uint64_t end = 0;
  bool operator==(const FindMarkRun&) const = default;
};

struct FindMarkScale {
  double join = 0.0;
  double shortest = 0.0;
};

std::vector<FindMarkRun> findMarkRuns(const FindMatches& matches, std::uint32_t decoder,
                                      std::span<const bitoscope::decode::AnnotationRow> rows, std::uint64_t begin,
                                      std::uint64_t end, FindMarkScale scale, std::size_t visits);

}
