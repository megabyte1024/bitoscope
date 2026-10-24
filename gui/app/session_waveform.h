/*
 * SPDX-License-Identifier: MIT
 */

// WHY: A running capture's buffer is read only while the panel holds its LiveCaptureSlot View;
// a source of an older generation is rebuilt before anything is read.
#pragma once

#include <capture_session.h>
#include <capture_waveform_source.h>
#include <color_choice.h>
#include <column_cache.h>
#include <cursors.h>
#include <extent_waveform_source.h>
#include <lane_header.h>
#include <lane_order.h>
#include <live_tail_source.h>
#include <live_view.h>
#include <marker_list.h>
#include <pass_uploads.h>
#include <trigger_spec.h>
#include <viewport.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "find_box.h"

namespace bitoscope::gui {

struct SessionWaveform {
  std::uint64_t view = 0;

  enum class Shows { Nothing, Live, Kept };
  Shows shows = Shows::Nothing;
  std::shared_ptr<capture::CaptureSession> session;
  std::shared_ptr<const capture::RawCaptureBuffer> keptBuffer;
  std::uint64_t liveGeneration = 0;
  std::unique_ptr<waveform_capture_source::CaptureWaveformSource> store;
  capture::CaptureTail tail;
  std::unique_ptr<waveform_capture_source::LiveTailWaveformSource> tailed;
  std::unique_ptr<waveform_view::ExtentWaveformSource> extent;
  std::unique_ptr<waveform_view::EmptyWaveformSource> empty;
  const waveform_view::WaveformSource* source = nullptr;
  std::vector<std::uint32_t> laneDevice;
  std::uint64_t requestedSamples = 0;
  std::uint64_t lastTotal = 0;
  std::uint64_t timeZero = 0;
  std::string note;

  std::optional<waveform_view::Viewport> viewport;
  bool fitted = false;
  bool viewOfCapture = false;
  std::optional<waveform_view::CarriedZoom> carriedZoom;
  bool showCursors = false;
  std::optional<waveform_view::CursorPair> cursors;
  int draggedCursorIndexOrMinusOne = -1;
  bool draggedCursorPair = false;
  double pairPressPixelX = 0;
  waveform_view::CursorPair pairPressCursors{0, 0};
  double grabbedSample = 0;
  double panScrollAtPress = 0;
  double panPressY = 0;

  waveform_view::MarkerList markers;
  std::optional<waveform_view::MarkerId> draggedMarker;
  std::optional<waveform_view::MarkerId> contextMarker;
  std::optional<waveform_view::MarkerId> renamingMarker;
  std::array<char, 64> renameBuffer{};
  bool renameFocusPending = false;

  std::optional<waveform_view::MarkerId> selectedMarker;
  bool rulerPressDragged = false;
  bool tracesPressDragged = false;
  int tracesPressClicks = 0;
  bool tracesPressOnLine = false;
  bool tracesPreviousPressDragged = false;

  std::optional<std::uint32_t> colorPickerChannel;
  std::array<float, 4> colorDraft{};
  std::optional<std::string> colorPickerDecoder;
  struct DecoderNameRefusal {
    std::string decoderId;
    std::string message;
    int frame = 0;
  };
  std::optional<DecoderNameRefusal> decoderNameRefusal;
  std::optional<std::pair<std::string, std::uint32_t>> rowFormatTarget;

  gui_lane_header::DragState laneDrag;
  std::optional<waveform_view::LaneKey> laneMenuKey;
  std::optional<waveform_view::LaneKey> headerMenuLane;
  std::uint32_t headerMenuChannel = 0;
  gui_lane_header::NameEdit nameEdit;

  FindBox find;

  gui_waveform_frame::ColumnCache columns;
  waveform_view::PassUploads passUploads;
};

}
