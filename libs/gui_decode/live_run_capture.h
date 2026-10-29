/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <atomic>
#include <cstdint>
#include <expected>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

#include "decoder.h"
#include "decoder_runner.h"
#include "growing_capture.h"
#include "signal_view_source.h"

namespace bitoscope::gui_decode {

class LiveRunCapture {
 public:
  LiveRunCapture(std::shared_ptr<const decoder_host::GrowingCapture> growing, std::uint64_t boundSamples);

  const std::shared_ptr<const decoder_host::GrowingCapture>& growing() const { return growing_; }
  std::shared_ptr<const decoder_host::CaptureEnd> end() const { return end_; }
  std::uint64_t boundSamples() const { return boundSamples_; }

  void keep(RunCapture finished, SignalViewSource views);
  std::optional<RunCapture> finished() const;
  std::expected<std::shared_ptr<const bitoscope::decode::SignalView>, std::string> finishedView(
    const std::atomic<bool>& stop) const;

 private:
  const std::shared_ptr<const decoder_host::GrowingCapture> growing_;
  const std::uint64_t boundSamples_;
  const std::shared_ptr<decoder_host::CaptureEnd> end_ = std::make_shared<decoder_host::CaptureEnd>();
  mutable std::mutex mutex_;
  std::optional<RunCapture> finished_;
  SignalViewSource views_;
};

}
