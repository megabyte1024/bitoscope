/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "acquisition.h"
#include "gui_event_queue.h"

namespace bitoscope::gui_events {

class PostingAcquisitionEvents final : public bitoscope::acquisition::AcquisitionEvents {
 public:
  PostingAcquisitionEvents(GuiEventQueue& queue, uint64_t run);

  void planned(uint32_t device, const bitoscope::capture::CapturePlan& plan,
               const std::optional<bitoscope::trigger::TriggerSpec>& trigger) override;
  void warning(uint32_t device, const bitoscope::acquisition::AcquireWarning& warn) override;
  void propertiesSet(uint32_t device, std::string_view driverDeviceId,
                      const std::vector<bitoscope::ipc::props::PropertyValue>& values) override;
  void started(uint32_t device, std::string_view driverDeviceId, uint64_t captureId) override;
  bool wantsProgress() const override;
  void progress(uint32_t device, uint64_t nowMs,
                const bitoscope::acquisition::Progress& snapshot) override;
  void waitEnded(uint32_t device) override;
  void ringPeak(uint32_t device, uint64_t peakSlotsInUse, uint32_t slotCount) override;
  void stopped(uint32_t device) override;
  void retrying(uint32_t device, const bitoscope::acquisition::AcquireRetry& retry) override;
  void completed(uint32_t device, const bitoscope::capture::CaptureSummary& summary,
                 const bitoscope::capture::CapturePlan& plan,
                 const bitoscope::capture::CaptureSession& session,
                 const std::optional<bitoscope::capture::MemoryShortfall>& shortfall) override;

 private:
  GuiEventQueue& queue_;
  uint64_t run_ = 0;
};

}
