/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <atomic>
#include <cstdint>
#include <expected>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <thread>

#include "acquisition.h"
#include "capture_settings.h"
#include "gui_event_queue.h"
#include "live_capture_slot.h"

namespace bitoscope::gui_capture {

struct RunControl {
  std::atomic<bool> stop{false};
  std::atomic<bool> abandon{false};
};

class CaptureRunner {
 public:
  using Body = std::function<void(const RunControl& control)>;

  explicit CaptureRunner(Body body);
  ~CaptureRunner();
  CaptureRunner(const CaptureRunner&) = delete;
  CaptureRunner& operator=(const CaptureRunner&) = delete;

  void requestStop();
  void requestAbandon();
  bool done() const;
  void join();

 private:
  RunControl control_;
  std::atomic<bool> done_{false};
  std::thread thread_;
};

using AcquireSessionFn = std::function<std::expected<bitoscope::acquisition::SessionResult, std::string>(
    std::span<const bitoscope::acquisition::SessionDevice> devices,
    const bitoscope::acquisition::SessionRequest& session, const bitoscope::acquisition::AcquisitionSeams& seams,
    bitoscope::acquisition::AcquisitionEvents& events)>;

struct CaptureRunSetup {
  uint64_t run = 0;
  bitoscope::host::pipe::PluginDevice device;
  bitoscope::host::FacetTransport* transport = nullptr;
  CaptureRequest request;
  std::function<std::optional<std::string>()> pluginClosed;
  std::function<std::optional<std::string>()> deviceLost;
  std::function<std::optional<bitoscope::ipc::la::CaptureEnded>(uint64_t captureId)> captureEnded;
  bitoscope::capture::MemoryProbe* probe = nullptr;
  bitoscope::capture::BlockSource* blocks = nullptr;
  std::function<uint64_t()> nowMs;
  std::function<void(uint64_t)> sleepMs;
};

void runCapture(const CaptureRunSetup& setup, const RunControl& control, bitoscope::gui_events::GuiEventQueue& queue,
                LiveCaptureSlot& slot, const AcquireSessionFn& acquire);

}
