/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "decoder.h"
#include "lane_display.h"

namespace bitoscope::gui_decode {

std::vector<std::uint32_t> findLaneRanks(const bitoscope::waveform_view::LaneDisplay& display,
                                         std::size_t decoderCount);

std::vector<std::uint32_t> findRowRanks(std::span<const bitoscope::decode::AnnotationRow> shownRows);

std::optional<std::size_t> shownRowSlot(std::span<const bitoscope::decode::AnnotationRow> shownRows,
                                        std::uint32_t row);

std::optional<double> findRowScrollTop(const bitoscope::waveform_view::LaneDisplay& display, std::uint32_t decoder,
                                       std::optional<std::size_t> slot, double rowPitch);

}
