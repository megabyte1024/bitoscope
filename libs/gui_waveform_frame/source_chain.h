/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

#include "capture_extent.h"
#include "capture_session.h"
#include "capture_waveform_source.h"
#include "extent_waveform_source.h"
#include "live_tail_source.h"
#include "waveform_source.h"

namespace bitoscope::gui_waveform_frame {

struct LiveSource {
  std::shared_ptr<capture::CaptureSession> session;
  uint64_t generation = 0;
  uint64_t requestedSamples = 0;
  std::span<const uint32_t> deviceChannels;
  uint64_t sampleRateMilliHz = 0;
};

struct KeptSource {
  std::shared_ptr<const capture::RawCaptureBuffer> buffer;
  std::shared_ptr<capture::CaptureSession> session;
  uint64_t requestedSamples = 0;
  std::span<const uint32_t> deviceChannels;
  capture::CaptureExtent extent;
  uint64_t timeZero = 0;
};

struct IdleSource {
  std::span<const uint32_t> devices;
  uint64_t sampleRateMilliHz = 0;
  uint64_t requestedSamples = 0;
};

struct SourceChoice {
  std::optional<LiveSource> live;
  std::optional<KeptSource> kept;
  IdleSource idle;
};

enum class SourceChange : uint8_t { None, ReplacedCapture, NewExtent, ReplacedIdle };

class WaveformSourceChain {
 public:
  enum class Shows : uint8_t { Nothing, Live, Kept };

  WaveformSourceChain() = default;
  WaveformSourceChain(const WaveformSourceChain&) = delete;
  WaveformSourceChain& operator=(const WaveformSourceChain&) = delete;

  SourceChange sync(const SourceChoice& choice);
  void drop();

  bool snapshotAdvanced();

  Shows shows() const { return shows_; }
  const waveform_view::WaveformSource* source() const { return source_; }
  const std::shared_ptr<capture::CaptureSession>& session() const { return session_; }
  const std::vector<uint32_t>& laneDevice() const { return laneDevice_; }
  uint64_t requestedSamples() const { return requestedSamples_; }
  uint64_t timeZero() const { return timeZero_; }

 private:
  SourceChange syncLive(const LiveSource& live);
  SourceChange syncKept(const KeptSource& kept);
  SourceChange syncIdle(const IdleSource& idle);

  Shows shows_ = Shows::Nothing;
  std::shared_ptr<capture::CaptureSession> session_;
  std::shared_ptr<const capture::RawCaptureBuffer> keptBuffer_;
  capture::CaptureExtent keptExtent_;
  uint64_t liveGeneration_ = 0;
  std::optional<uint64_t> observedSnapshotGeneration_;
  std::unique_ptr<waveform_capture_source::CaptureWaveformSource> store_;
  capture::CaptureTail tail_;
  std::unique_ptr<waveform_capture_source::LiveTailWaveformSource> tailed_;
  std::unique_ptr<waveform_view::ExtentWaveformSource> extent_;
  std::unique_ptr<waveform_view::EmptyWaveformSource> empty_;
  const waveform_view::WaveformSource* source_ = nullptr;
  std::vector<uint32_t> laneDevice_;
  uint64_t requestedSamples_ = 0;
  uint64_t timeZero_ = 0;
};

}
