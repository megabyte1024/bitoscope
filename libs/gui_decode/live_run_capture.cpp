/*
 * SPDX-License-Identifier: MIT
 */

#include "live_run_capture.h"

#include <utility>

namespace bitoscope::gui_decode {

LiveRunCapture::LiveRunCapture(std::shared_ptr<const decoder_host::GrowingCapture> growing, std::uint64_t boundSamples)
    : growing_(std::move(growing)), boundSamples_(boundSamples) {}

void LiveRunCapture::keep(RunCapture finished, SignalViewSource views) {
  bitoscope::capture::CaptureExtent stored;
  {
    const std::lock_guard lock(mutex_);
    if (finished_) return;
    stored = finished.extent;
    finished_ = std::move(finished);
    views_ = std::move(views);
  }
  end_->keep(stored);
}

std::optional<RunCapture> LiveRunCapture::finished() const {
  const std::lock_guard lock(mutex_);
  return finished_;
}

std::expected<std::shared_ptr<const bitoscope::decode::SignalView>, std::string> LiveRunCapture::finishedView(
  const std::atomic<bool>& stop) const {
  for (;;) {
    SignalViewSource views;
    {
      const std::lock_guard lock(mutex_);
      views = views_;
    }
    if (views) return views();
    if (stop.load()) return std::unexpected(std::string("cancelled"));
    (void)end_->waitForStored(decoder_host::kGrowingFeedWait);
  }
}

}
