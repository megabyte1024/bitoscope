/*
 * SPDX-License-Identifier: MIT
 */

#include "posting_acquisition_events.h"

namespace bitoscope::gui_events {

PostingAcquisitionEvents::PostingAcquisitionEvents(GuiEventQueue& queue, uint64_t run) : queue_(queue), run_(run) {}

void PostingAcquisitionEvents::planned(uint32_t device, const bitoscope::capture::CapturePlan& plan,
                                        const std::optional<bitoscope::trigger::TriggerSpec>& trigger) {
  queue_.post([run = run_, device, plan, trigger](const GuiEventHandlers& handlers) {
    if (handlers.onPlanned) handlers.onPlanned(run, device, plan, trigger);
  });
}

void PostingAcquisitionEvents::warning(uint32_t device,
                                        const bitoscope::acquisition::AcquireWarning& warn) {
  queue_.post([run = run_, device, warn](const GuiEventHandlers& handlers) {
    if (handlers.onWarning) handlers.onWarning(run, device, warn);
  });
}

void PostingAcquisitionEvents::propertiesSet(
    uint32_t device, std::string_view pluginId,
    const std::vector<bitoscope::ipc::props::PropertyValue>& values) {
  std::string id(pluginId);
  queue_.post([run = run_, device, id, values](const GuiEventHandlers& handlers) {
    if (handlers.onPropertiesSet) handlers.onPropertiesSet(run, device, id, values);
  });
}

void PostingAcquisitionEvents::started(uint32_t device, std::string_view pluginId,
                                        uint64_t captureId) {
  std::string id(pluginId);
  queue_.post([run = run_, device, id, captureId](const GuiEventHandlers& handlers) {
    if (handlers.onStarted) handlers.onStarted(run, device, id, captureId);
  });
}

bool PostingAcquisitionEvents::wantsProgress() const { return true; }

void PostingAcquisitionEvents::progress(uint32_t device, uint64_t nowMs,
                                         const bitoscope::acquisition::Progress& snapshot) {
  queue_.post([run = run_, device, nowMs, snapshot](const GuiEventHandlers& handlers) {
    if (handlers.onProgress) handlers.onProgress(run, device, nowMs, snapshot);
  });
}

void PostingAcquisitionEvents::waitEnded(uint32_t device) {
  queue_.post([run = run_, device](const GuiEventHandlers& handlers) {
    if (handlers.onWaitEnded) handlers.onWaitEnded(run, device);
  });
}

void PostingAcquisitionEvents::ringPeak(uint32_t device, uint64_t peakSlotsInUse,
                                         uint32_t slotCount) {
  queue_.post([run = run_, device, peakSlotsInUse, slotCount](const GuiEventHandlers& handlers) {
    if (handlers.onRingPeak) handlers.onRingPeak(run, device, peakSlotsInUse, slotCount);
  });
}

void PostingAcquisitionEvents::stopped(uint32_t device) {
  queue_.post([run = run_, device](const GuiEventHandlers& handlers) {
    if (handlers.onStopped) handlers.onStopped(run, device);
  });
}

void PostingAcquisitionEvents::retrying(uint32_t device,
                                         const bitoscope::acquisition::AcquireRetry& retry) {
  queue_.post([run = run_, device, retry](const GuiEventHandlers& handlers) {
    if (handlers.onRetrying) handlers.onRetrying(run, device, retry);
  });
}

void PostingAcquisitionEvents::completed(
    uint32_t device, const bitoscope::capture::CaptureSummary& summary,
    const bitoscope::capture::CapturePlan& plan, const bitoscope::capture::CaptureSession& session,
    const std::optional<bitoscope::capture::MemoryShortfall>& shortfall) {
  // WHY: Read triggerSampleIndex during the call: the session is not safe to touch after completed() returns.
  const std::optional<uint64_t> triggerSampleIndex = session.triggerSampleIndex();
  queue_.post([run = run_, device, summary, plan, triggerSampleIndex, shortfall](const GuiEventHandlers& handlers) {
    if (handlers.onCompleted) handlers.onCompleted(run, device, summary, plan, triggerSampleIndex, shortfall);
  });
}

}
