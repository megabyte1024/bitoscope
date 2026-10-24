/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <set>

#include "row_class.h"

namespace bitoscope::waveform_view {

struct DecoderVisibility {
  std::set<RowClass> hiddenClasses;
  bool operator==(const DecoderVisibility&) const = default;
};

inline bool classHidden(const DecoderVisibility& visibility, std::uint32_t row, std::uint32_t cls) {
  return visibility.hiddenClasses.contains({row, cls});
}

}
