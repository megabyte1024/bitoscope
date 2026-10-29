/*
 * SPDX-License-Identifier: MIT
 */

#include "decode_signal_view.h"

#include <memory>
#include <utility>

namespace bitoscope::gui_decode {

namespace {

// WHY: ViewOverBuffer keeps the buffer beside the view: the view reads it through a plain pointer.
struct ViewOverBuffer {
  std::shared_ptr<const bitoscope::capture::RawCaptureBuffer> buffer;
  bitoscope::capture::RawBufferSignalView view;
};

bitoscope::capture::ExtentTail extentTailOf(const bitoscope::gui_capture::CaptureOutcome& outcome) {
  if (outcome.trimmed) return bitoscope::capture::ExtentTail::Trimmed;
  return outcome.originSampleIndex.has_value() ? bitoscope::capture::ExtentTail::InsideLastWindow
                                               : bitoscope::capture::ExtentTail::Exact;
}

}

std::expected<bitoscope::capture::RawBufferSignalView, std::string> signalViewForOutcome(
    const bitoscope::gui_capture::CaptureOutcome& outcome) {
  if (!outcome.kept()) {
    return std::unexpected("there is no capture to decode: last run kept nothing");
  }
  if (!outcome.buffer) {
    return std::unexpected("capture's samples are not held any more; nothing can be decoded");
  }
  return bitoscope::capture::RawBufferSignalView::create(*outcome.buffer, outcome.extent, extentTailOf(outcome));
}

std::expected<SignalViewSource, std::string> signalViewSourceForOutcome(
    const bitoscope::gui_capture::CaptureOutcome& outcome) {
  if (auto refused = signalViewForOutcome(outcome); !refused) return std::unexpected(refused.error());
  const std::shared_ptr<const bitoscope::capture::RawCaptureBuffer> buffer = outcome.buffer;
  const bitoscope::capture::CaptureExtent extent = outcome.extent;
  const bitoscope::capture::ExtentTail tail = extentTailOf(outcome);
  return SignalViewSource(
    [buffer, extent, tail]() -> std::expected<std::shared_ptr<const bitoscope::decode::SignalView>, std::string> {
      auto view = bitoscope::capture::RawBufferSignalView::create(*buffer, extent, tail);
      if (!view) return std::unexpected(view.error());
      auto made = std::make_shared<const ViewOverBuffer>(ViewOverBuffer{buffer, std::move(*view)});
      return std::shared_ptr<const bitoscope::decode::SignalView>(made, &made->view);
    });
}

std::expected<RunCapture, std::string> runCaptureForOutcome(const bitoscope::gui_capture::CaptureOutcome& outcome) {
  if (auto refused = signalViewForOutcome(outcome); !refused) return std::unexpected(refused.error());
  const bitoscope::capture::ExtentTail tail = extentTailOf(outcome);
  return RunCapture{outcome.buffer, outcome.extent, tail};
}

}
