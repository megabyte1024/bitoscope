/*
 * SPDX-License-Identifier: MIT
 */

#include "frame_render.h"

#include <algorithm>
#include <exception>
#include <numeric>
#include <string>
#include <vector>

#include "dense_span.h"
#include "viewport.h"

namespace bitoscope::gui_waveform_frame {

namespace wv = bitoscope::waveform_view;

namespace {

std::string whatOf(const std::exception_ptr& error) {
  try {
    std::rethrow_exception(error);
  } catch (const std::exception& e) {
    return e.what();
  } catch (...) {
    return "it threw something carrying no message";
  }
}

}

std::uint32_t renderLanes(std::uint32_t hardwareThreads) {
  return (std::max)(std::uint32_t{1}, (std::min)(hardwareThreads / 2, kMaxRenderLanes));
}

std::uint32_t lanesForFrame(std::uint32_t channels, std::uint32_t widthPixels,
                            std::uint32_t poolLanes) {
  if (channels == 0 || widthPixels == 0 || poolLanes <= 1) return 1;
  if (std::uint64_t{channels} * widthPixels < kMinThreadedFrameColumns) return 1;
  return (std::min)(poolLanes, channels);
}

FrameRender renderFrame(const wv::WaveformSource& source, std::uint32_t channels,
                        const wv::Viewport& viewport, std::uint32_t lanes,
                        gui_task_pool::TaskPool* pool, std::span<std::uint8_t> levels,
                        std::span<std::vector<double>> dots,
                        std::span<std::vector<wv::DenseSpan>> spans) {
  std::fill(levels.begin(), levels.end(), static_cast<std::uint8_t>(wv::ColumnLevel::NoData));
  std::vector<std::uint32_t> every(channels);
  std::iota(every.begin(), every.end(), std::uint32_t{0});
  return renderFrame(source, channels, every, viewport, lanes, pool, levels, dots, spans);
}

FrameRender renderRows(std::uint32_t channels, std::span<const std::uint32_t> rows, std::uint32_t lanes,
                       gui_task_pool::TaskPool* pool, const RowRender& renderRow) {
  FrameRender out;
  out.errors.assign(channels, std::string());

  // WHY: Sorted and unique: a row listed twice could land in two lanes, and lanes must not share a channel.
  std::vector<std::uint32_t> order;
  order.reserve(rows.size());
  for (const std::uint32_t c : rows) {
    if (c < channels) order.push_back(c);
  }
  std::sort(order.begin(), order.end());
  order.erase(std::unique(order.begin(), order.end()), order.end());
  if (order.empty()) return out;

  const auto count = static_cast<std::uint32_t>(order.size());
  if (pool == nullptr || lanes == 0) lanes = 1;
  if (pool != nullptr) lanes = (std::min)(lanes, pool->lanes());
  lanes = (std::min)(lanes, count);
  out.lanes = lanes;

  // WHY: Each element is written only by the lane that owns its channel, so no lock is needed.
  std::vector<std::uint8_t> drawn(channels, 0);

  const auto renderOne = [&](std::uint32_t c) {
    if (std::optional<std::string> error = renderRow(c)) out.errors[c] = std::move(*error);
    drawn[c] = 1;
  };

  // WHY: Lanes get contiguous, disjoint ranges of the rows: no two lanes pass one channel to the source.
  const auto laneBody = [&](std::uint32_t lane) {
    const auto first = static_cast<std::uint32_t>(std::uint64_t{count} * lane / lanes);
    const auto last = static_cast<std::uint32_t>(std::uint64_t{count} * (lane + 1) / lanes);
    for (std::uint32_t i = first; i < last; ++i) renderOne(order[i]);
  };

  if (lanes == 1) {
    try {
      laneBody(0);
    } catch (...) {
      out.laneFailures.push_back(whatOf(std::current_exception()));
    }
  } else {
    for (const gui_task_pool::TaskFailure& failed : pool->runLanes(lanes, laneBody)) {
      out.laneFailures.push_back(failed.reason);
    }
  }

  if (!out.laneFailures.empty()) {
    for (const std::uint32_t c : order) {
      if (drawn[c] != 0) continue;
      try {
        renderOne(c);
      } catch (...) {
        out.errors[c] = whatOf(std::current_exception());
      }
    }
  }

  for (const std::string& error : out.errors) {
    if (!error.empty()) out.anyError = true;
  }
  return out;
}

FrameRender renderFrame(const wv::WaveformSource& source, std::uint32_t channels,
                        std::span<const std::uint32_t> rows, const wv::Viewport& viewport,
                        std::uint32_t lanes, gui_task_pool::TaskPool* pool, std::span<std::uint8_t> levels,
                        std::span<std::vector<double>> dots, std::span<std::vector<wv::DenseSpan>> spans) {
  const std::uint32_t width = viewport.widthPixels();
  for (const std::uint32_t c : rows) {
    if (c >= channels) continue;
    const std::size_t at = static_cast<std::size_t>(c) * width;
    if (at >= levels.size()) continue;
    const std::size_t count = (std::min)(static_cast<std::size_t>(width), levels.size() - at);
    std::fill_n(levels.begin() + static_cast<std::ptrdiff_t>(at), count,
                static_cast<std::uint8_t>(wv::ColumnLevel::NoData));
  }

  return renderRows(channels, rows, lanes, pool, [&](std::uint32_t c) -> std::optional<std::string> {
    if (c < spans.size()) spans[c].clear();
    auto row = wv::renderChannel(source, c, viewport);
    if (!row) return std::move(row.error());
    const std::size_t at = static_cast<std::size_t>(c) * width;
    if (at + row->columns.size() <= levels.size()) {
      std::copy(row->columns.begin(), row->columns.end(), levels.begin() + static_cast<std::ptrdiff_t>(at));
    }
    if (c < dots.size()) dots[c] = std::move(row->samplePointsX);
    if (c < spans.size()) spans[c] = wv::denseSpansOf(source, c, viewport, row->columns);
    return std::nullopt;
  });
}

}
