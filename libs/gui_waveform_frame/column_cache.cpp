/*
 * SPDX-License-Identifier: MIT
 */

#include "column_cache.h"

#include <algorithm>
#include <cstddef>
#include <utility>

#include "channel_render.h"

namespace bitoscope::gui_waveform_frame {

namespace wv = bitoscope::waveform_view;

namespace {

constexpr auto kNoData = static_cast<std::uint8_t>(wv::ColumnLevel::NoData);

}

void ColumnCache::forget() { valid_ = false; }

std::span<const std::uint8_t> ColumnCache::row(std::uint32_t channel) const {
  if (channel >= channels_) return {};
  return std::span<const std::uint8_t>(levels_).subspan(static_cast<std::size_t>(channel) * width_, width_);
}

std::span<std::uint8_t> ColumnCache::rawRow(std::uint32_t channel) {
  return std::span<std::uint8_t>(raw_).subspan(static_cast<std::size_t>(channel) * width_, width_);
}

void ColumnCache::reset(const wv::Viewport& columns, std::uint32_t channels, std::uint64_t total,
                        std::uint64_t summaryPendingBefore) {
  const std::uint32_t width = columns.widthPixels();
  raw_.assign(static_cast<std::size_t>(width) * channels, kNoData);
  levels_.assign(static_cast<std::size_t>(width) * channels, kNoData);
  dots_.assign(channels, {});
  spans_.assign(channels, {});
  probes_.assign(channels, {});
  failed_.assign(channels, 0);
  held_.forget(channels);
  valid_ = true;
  firstVisibleSample_ = columns.firstVisibleSample();
  samplesPerPixel_ = columns.samplesPerPixel();
  width_ = width;
  channels_ = channels;
  total_ = total;
  summaryPendingBefore_ = summaryPendingBefore;
}

void ColumnCache::release(std::uint32_t channel) {
  const std::span<std::uint8_t> raw = rawRow(channel);
  std::fill(raw.begin(), raw.end(), kNoData);
  std::fill_n(levels_.begin() + static_cast<std::ptrdiff_t>(static_cast<std::size_t>(channel) * width_), width_,
              kNoData);
  dots_[channel].clear();
  spans_[channel].clear();
  probes_[channel] = {};
  failed_[channel] = 0;
}

void ColumnCache::flatten(std::uint32_t channel) {
  const std::span<std::uint8_t> raw = rawRow(channel);
  const auto level = std::span<std::uint8_t>(levels_).subspan(static_cast<std::size_t>(channel) * width_, width_);
  std::copy(raw.begin(), raw.end(), level.begin());
  // WHY: Dense runs are painted as whole solid columns on the flat level, so their width stays stable on scroll.
  if (!spans_[channel].empty()) wv::flattenDenseColumns(level, spans_[channel]);
}

std::optional<std::string> ColumnCache::renderWhole(const wv::WaveformSource& source, std::uint32_t channel,
                                                    const wv::Viewport& columns) {
  spans_[channel].clear();
  probes_[channel] = {};
  auto drawn = wv::renderChannel(source, channel, columns);
  if (!drawn) return std::move(drawn.error());
  const std::span<std::uint8_t> raw = rawRow(channel);
  std::copy(drawn->columns.begin(), drawn->columns.end(), raw.begin());
  dots_[channel] = std::move(drawn->samplePointsX);
  spans_[channel] = wv::denseSpansOf(source, channel, columns, raw, &probes_[channel]);
  flatten(channel);
  return std::nullopt;
}

std::optional<std::string> ColumnCache::renderFrom(const wv::WaveformSource& source, std::uint32_t channel,
                                                   const wv::Viewport& columns, std::uint32_t firstColumn) {
  const std::span<std::uint8_t> raw = rawRow(channel);
  if (auto drawn = wv::renderChannelColumns(source, channel, columns, firstColumn, raw); !drawn) {
    return std::move(drawn.error());
  }
  dots_[channel] = wv::samplePointsOf(columns, total_);
  wv::extendDenseSpans(source, channel, columns, raw, firstColumn, spans_[channel], probes_[channel]);
  flatten(channel);
  return std::nullopt;
}

void ColumnCache::take(ColumnUpdate& out, FrameRender&& drawn, std::span<const std::uint32_t> rows) {
  for (const std::uint32_t c : rows) {
    failed_[c] = drawn.errors[c].empty() ? 0 : 1;
    if (!drawn.errors[c].empty()) out.errors[c] = std::move(drawn.errors[c]);
  }
  out.anyError = out.anyError || drawn.anyError;
  for (std::string& failure : drawn.laneFailures) out.laneFailures.push_back(std::move(failure));
}

ColumnUpdate ColumnCache::update(const wv::WaveformSource& source, std::span<const std::uint32_t> wanted,
                                 const wv::Viewport& columns, gui_task_pool::TaskPool* pool) {
  ColumnUpdate out;
  const std::uint32_t channels = source.channelCount();
  const std::uint64_t total = source.sampleCount();
  const std::uint64_t summaryPendingBefore = source.summaryPendingBefore();
  const std::uint32_t width = columns.widthPixels();
  out.errors.assign(channels, std::string());
  const std::uint32_t poolLanes = pool != nullptr ? pool->lanes() : 1;
  const bool sameColumns = valid_ && firstVisibleSample_ == columns.firstVisibleSample() &&
                           samplesPerPixel_ == columns.samplesPerPixel() && width_ == width &&
                           channels_ == channels && summaryPendingBefore_ == summaryPendingBefore;
  // WHY: Growth keeps columns only with no summary pending: pending history columns change as its tail is stored.
  const bool grew = sameColumns && total > total_ && summaryPendingBefore == 0;
  if (!sameColumns || (total != total_ && !grew)) {
    reset(columns, channels, total, summaryPendingBefore);
    out.rowsMade = true;
  } else if (grew) {
    const std::uint32_t firstChanged = wv::firstColumnReachingPast(columns, total_);
    total_ = total;
    std::vector<std::uint8_t> extended(channels, 0);
    std::vector<std::uint32_t> rows;
    for (const std::uint32_t c : wanted) {
      if (c < channels && held_.holds(c) && failed_[c] == 0 && extended[c] == 0) {
        extended[c] = 1;
        rows.push_back(c);
      }
    }
    for (std::uint32_t c = 0; c < channels; ++c) {
      if (held_.holds(c) && extended[c] == 0) release(c);
    }
    held_.forget(channels);
    held_.hold(rows);
    const std::uint32_t pastData = (std::min)(width, wv::firstColumnReachingPast(columns, total) + 1);
    const std::uint32_t changed = pastData > firstChanged ? pastData - firstChanged : 0;
    const std::uint32_t lanes = lanesForFrame(static_cast<std::uint32_t>(rows.size()), changed, poolLanes);
    take(out,
         renderRows(channels, rows, lanes, pool,
                    [&](std::uint32_t c) { return renderFrom(source, c, columns, firstChanged); }),
         rows);
    out.rowsMade = true;
  }

  const std::vector<std::uint32_t> missing = held_.missing(wanted);
  if (missing.empty()) return out;
  const std::uint32_t lanes = lanesForFrame(static_cast<std::uint32_t>(missing.size()), width, poolLanes);
  take(out, renderRows(channels, missing, lanes, pool, [&](std::uint32_t c) { return renderWhole(source, c, columns); }),
       missing);
  held_.hold(missing);
  out.rowsMade = true;
  return out;
}

}
