/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "dense_span.h"
#include "frame_render.h"
#include "held_rows.h"
#include "task_pool.h"
#include "viewport.h"
#include "waveform_source.h"

namespace bitoscope::gui_waveform_frame {

struct ColumnUpdate {
  bool rowsMade = false;
  std::vector<std::string> errors;
  bool anyError = false;
  std::vector<std::string> laneFailures;
};

class ColumnCache {
 public:
  void forget();

  ColumnUpdate update(const waveform_view::WaveformSource& source, std::span<const std::uint32_t> wanted,
                      const waveform_view::Viewport& columns, gui_task_pool::TaskPool* pool);

  std::uint32_t channels() const { return channels_; }
  std::uint32_t width() const { return width_; }
  std::span<const std::uint8_t> levels() const { return levels_; }
  std::span<const std::uint8_t> row(std::uint32_t channel) const;
  const std::vector<std::vector<double>>& dots() const { return dots_; }
  const std::vector<std::vector<waveform_view::DenseSpan>>& spans() const { return spans_; }

 private:
  void reset(const waveform_view::Viewport& columns, std::uint32_t channels, std::uint64_t total,
             std::uint64_t summaryPendingBefore);
  void release(std::uint32_t channel);
  std::span<std::uint8_t> rawRow(std::uint32_t channel);
  void flatten(std::uint32_t channel);
  std::optional<std::string> renderWhole(const waveform_view::WaveformSource& source, std::uint32_t channel,
                                         const waveform_view::Viewport& columns);
  std::optional<std::string> renderFrom(const waveform_view::WaveformSource& source, std::uint32_t channel,
                                        const waveform_view::Viewport& columns, std::uint32_t firstColumn);
  void take(ColumnUpdate& out, FrameRender&& drawn, std::span<const std::uint32_t> rows);

  bool valid_ = false;
  double firstVisibleSample_ = 0.0;
  double samplesPerPixel_ = 0.0;
  std::uint32_t width_ = 0;
  std::uint32_t channels_ = 0;
  std::uint64_t total_ = 0;
  std::uint64_t summaryPendingBefore_ = 0;
  std::vector<std::uint8_t> raw_;
  std::vector<std::uint8_t> levels_;
  std::vector<std::vector<double>> dots_;
  std::vector<std::vector<waveform_view::DenseSpan>> spans_;
  std::vector<waveform_view::DenseProbes> probes_;
  std::vector<std::uint8_t> failed_;
  HeldRows held_;
};

}
