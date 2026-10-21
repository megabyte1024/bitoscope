/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

// WHY: Lanes get disjoint channel ranges: a shared source may keep mutable state only per channel.

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "channel_render.h"
#include "dense_span.h"
#include "task_pool.h"
#include "waveform_source.h"

namespace bitoscope::waveform_view {
class Viewport;
}

namespace bitoscope::gui_waveform_frame {

// WHY: Four lanes: eight scale only 4.5x at 56% efficiency (SMT) and take cores packers and decoders need.
inline constexpr std::uint32_t kMaxRenderLanes = 4;

// WHY: Below 2048 columns pool wake-up and join cost more than the split gives back (measured).
inline constexpr std::uint64_t kMinThreadedFrameColumns = 2048;

std::uint32_t renderLanes(std::uint32_t hardwareThreads);

std::uint32_t lanesForFrame(std::uint32_t channels, std::uint32_t widthPixels,
                            std::uint32_t poolLanes);

struct FrameRender {
  std::uint32_t lanes = 1;
  std::vector<std::string> errors;
  bool anyError = false;
  std::vector<std::string> laneFailures;
};

using RowRender = std::function<std::optional<std::string>(std::uint32_t row)>;

FrameRender renderRows(std::uint32_t channels, std::span<const std::uint32_t> rows, std::uint32_t lanes,
                       gui_task_pool::TaskPool* pool, const RowRender& renderRow);

FrameRender renderFrame(const waveform_view::WaveformSource& source, std::uint32_t channels,
                        const waveform_view::Viewport& viewport, std::uint32_t lanes,
                        gui_task_pool::TaskPool* pool, std::span<std::uint8_t> levels,
                        std::span<std::vector<double>> dots,
                        std::span<std::vector<waveform_view::DenseSpan>> spans = {});

FrameRender renderFrame(const waveform_view::WaveformSource& source, std::uint32_t channels,
                        std::span<const std::uint32_t> rows, const waveform_view::Viewport& viewport,
                        std::uint32_t lanes, gui_task_pool::TaskPool* pool, std::span<std::uint8_t> levels,
                        std::span<std::vector<double>> dots,
                        std::span<std::vector<waveform_view::DenseSpan>> spans = {});

}
