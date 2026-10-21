/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <vector>

#include "annotation_memory.h"
#include "capture_extent.h"
#include "decode_progress.h"
#include "decoder.h"
#include "decoder_chain.h"
#include "gui_event_queue.h"
#include "signal_view_source.h"

namespace bitoscope::capture {
class RawCaptureBuffer;
}

namespace bitoscope::gui_decode {

struct ConfiguredDecoderRun {
  std::unique_ptr<bitoscope::decode::Decoder> decoder;
};

struct DecodeMemory {
  std::uint64_t budgetBytes = std::numeric_limits<std::uint64_t>::max();
  std::uint64_t heldBytes = 0;
  std::shared_ptr<bitoscope::waveform_view::AnnotationMemory> memory;
};

using DecodeThreadSetup = std::function<void()>;

inline constexpr std::chrono::milliseconds kCancelLimit{250};

struct RunCapture {
  std::shared_ptr<const bitoscope::capture::RawCaptureBuffer> buffer;
  bitoscope::capture::CaptureExtent extent;
  bitoscope::capture::ExtentTail tail = bitoscope::capture::ExtentTail::Exact;
};

class DecoderRunner {
 public:
  DecoderRunner(std::uint64_t run, SignalViewSource views, bitoscope::decode::ChainPlan plan,
                bitoscope::gui_events::GuiEventQueue& queue, DecodeMemory memory = {},
                DecodeThreadSetup threadSetup = {}, ProgressClock clock = {},
                std::function<void(std::size_t instance)> beforeThreadStart = {},
                std::optional<RunCapture> capture = {});
  DecoderRunner(std::uint64_t run, const bitoscope::decode::SignalView& view, bitoscope::decode::ChainPlan plan,
                bitoscope::gui_events::GuiEventQueue& queue, DecodeMemory memory = {});
  DecoderRunner(std::uint64_t run, const bitoscope::decode::SignalView& view,
                std::vector<ConfiguredDecoderRun> decoders, bitoscope::gui_events::GuiEventQueue& queue,
                DecodeMemory memory = {});
  ~DecoderRunner();
  DecoderRunner(const DecoderRunner&) = delete;
  DecoderRunner& operator=(const DecoderRunner&) = delete;

  void cancel();
  bool done() const;
  void join();

 private:
  struct Work;

  std::atomic<bool> cancel_{false};
  std::unique_ptr<Work> work_;
};

}
