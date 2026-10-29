/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>

#include "capture_outcome.h"
#include "capture_session.h"
#include "capture_settings.h"
#include "decoder_runner.h"
#include "decoding_prefix.h"
#include "live_run_capture.h"

namespace bitoscope::gui_decode {

enum class LiveJobAtEnd { Keep, Cancel };

LiveJobAtEnd liveJobAtEnd(bitoscope::gui_capture::OutcomeKind kind);

inline constexpr std::uint64_t kLiveDecodeMaxBytesPerSecond = 1'000'000'000;

bool liveDecodingAllowed(const bitoscope::gui_capture::CaptureRequest& request);

std::span<const std::uint32_t> settingsChannels(const std::optional<bitoscope::gui_capture::CaptureRequest>& running,
                                                std::span<const std::uint32_t> kept);

struct DecodeRun {
  std::uint64_t id = 0;
  std::unique_ptr<DecoderRunner> runner;
};

struct LiveAttempt {
  std::shared_ptr<const bitoscope::capture::CaptureSession> session;
  std::uint64_t generation = 0;
  std::uint64_t boundSamples = 0;
};

enum class LiveStep { None, Started, Cancelled };

class PanelDecodes {
 public:
  using StartLive = std::function<DecodeRun(std::shared_ptr<const LiveRunCapture> capture)>;

  void run(DecodeRun run);
  void cancel();
  LiveStep follow(const LiveAttempt& attempt, bool allowed, const StartLive& start);
  bool captureEnded(const bitoscope::gui_capture::CaptureOutcome& outcome);
  bool decodeAgainAfterCapture(std::uint64_t run, std::size_t position) const;
  bitoscope::waveform_view::LaneCaptureMode laneCaptureMode(bool captureRunning, bool gateAllowed,
                                                            std::optional<std::size_t> runPosition) const;

  bool runsLive() const { return live_.has_value(); }
  std::uint64_t runId() const { return run_.id; }
  const DecoderRunner* runner() const { return run_.runner.get(); }

 private:
  struct Live {
    std::uint64_t generation = 0;
    std::shared_ptr<const bitoscope::capture::CaptureSession> session;
    std::shared_ptr<LiveRunCapture> capture;
    bool endedShort = false;
  };

  DecodeRun run_;
  std::optional<Live> live_;
  std::optional<std::uint64_t> tried_;
};

}
