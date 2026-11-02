/*
 * SPDX-License-Identifier: MIT
 */

#include "capture_runner.h"

#include <chrono>
#include <exception>
#include <memory>
#include <utility>
#include <vector>

#include "posting_acquisition_events.h"

namespace bitoscope::gui_capture {

CaptureRunner::CaptureRunner(Body body)
    : thread_([this, body = std::move(body)] {
        body(control_);
        done_ = true;
      }) {}

CaptureRunner::~CaptureRunner() {
  requestAbandon();
  join();
}

void CaptureRunner::requestStop() { control_.stop = true; }

void CaptureRunner::requestAbandon() { control_.abandon = true; }

bool CaptureRunner::done() const { return done_.load(); }

void CaptureRunner::join() {
  if (thread_.joinable()) thread_.join();
}

void runCapture(const CaptureRunSetup& setup, const RunControl& control, bitoscope::gui_events::GuiEventQueue& queue,
                LiveCaptureSlot& slot, const AcquireSessionFn& acquire) {
  using Result = std::expected<bitoscope::acquisition::SessionResult, std::string>;
  const uint64_t run = setup.run;
  Result result = std::unexpected(std::string("capture did not run"));
  try {
    if (setup.transport == nullptr || setup.probe == nullptr || setup.blocks == nullptr) {
      result = std::unexpected(std::string("capture has no device connection or no memory to run on"));
    } else {
      std::vector<bitoscope::acquisition::SessionDevice> devices;
      devices.push_back(bitoscope::acquisition::SessionDevice{setup.device, *setup.transport, setup.request.request,
                                                              setup.driverClosed, setup.deviceLost,
                                                              setup.captureEnded});
      bitoscope::acquisition::SessionRequest session;
      session.memory_policy = setup.request.request.memory_policy;
      session.memory_budget_bytes = setup.request.request.memory_budget_bytes;
      session.reserve_bytes = setup.request.request.reserve_bytes;
      session.keep_on_memory_full = true;
      session.start_retries = bitoscope::acquisition::SessionRequest{}.start_retries;

      bitoscope::acquisition::AcquisitionSeams seams{*setup.probe, *setup.blocks};
      seams.samples_per_window = setup.request.samplesPerWindow;
      seams.now_ms = setup.nowMs ? setup.nowMs : [] {
        return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                         std::chrono::steady_clock::now().time_since_epoch())
                                         .count());
      };
      seams.sleep_ms = setup.sleepMs ? setup.sleepMs
                                     : [](uint64_t ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); };
      seams.stop_requested = [&control] { return control.stop.load(); };
      seams.abandon_requested = [&control] { return control.abandon.load(); };
      seams.on_session = [&slot](std::shared_ptr<bitoscope::capture::CaptureSession> attempt) {
        slot.set(std::move(attempt));
      };
      seams.driver_closed = setup.driverClosed;
      seams.device_lost = setup.deviceLost;
      seams.capture_ended = setup.captureEnded;
      bitoscope::gui_events::PostingAcquisitionEvents events(queue, run);
      result = acquire(devices, session, seams, events);
    }
  } catch (const std::exception& e) {
    result = std::unexpected(std::string("capture failed: ") + e.what() + "; nothing was saved");
  } catch (...) {
    result = std::unexpected(std::string("capture failed with unknown error; nothing was saved"));
  }
  slot.set(nullptr);
  auto shared = std::make_shared<const Result>(std::move(result));
  queue.post([run, shared](const bitoscope::gui_events::GuiEventHandlers& handlers) {
    if (handlers.onCaptureFinished) handlers.onCaptureFinished(run, *shared);
  });
}

}
