/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "decoder.h"
#include "row_source.h"

namespace bitoscope::gui_decode {

bool rowTakesDataFormat(std::uint32_t row, std::span<const std::uint32_t> presentClasses,
                        const std::vector<bitoscope::decode::AnnotationClass>& classes,
                        const std::vector<bitoscope::decode::RowSource>& rowSources);

}
