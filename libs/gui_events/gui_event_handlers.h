/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

// WHY: onCompleted carries triggerSampleIndex, not a CaptureSession: the session cannot outlive the post/drain gap.

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "acquisition.h"
#include "capture_plan.h"
#include "capture_session.h"
#include "capture_writer.h"
#include "decoder.h"
#include "ipc_messages.h"
#include "ipc_properties.h"
#include "opened_device.h"
#include "progress.h"
#include "trigger_spec.h"

namespace bitoscope::gui_decode {
class DecodedAnnotations;
struct DecodeProgress;
}

namespace bitoscope::gui_events {

struct GuiEventHandlers {
  std::function<void(std::string_view driver, const bitoscope::ipc::DeviceInfo& device)>
      onDeviceAppeared;
  std::function<void(std::string_view driver, std::string_view deviceId, std::string_view reason)>
      onDeviceLost;
  std::function<void(std::string_view driver, std::string_view reason)> onWatchEnded;

  std::function<void(const std::filesystem::path& directory,
                     const std::vector<std::string>& drivers)>
      onDriversFound;
  std::function<void(std::string_view driver,
                     const std::expected<bitoscope::ipc::DeviceList, std::string>& list)>
      onDevicesListed;
  std::function<void(std::string_view driver, std::string_view deviceId,
                     const std::expected<bitoscope::host::OpenedDevice, bitoscope::host::OpenRefusal>& opened)>
      onDeviceOpened;

  std::function<void(std::string_view pluginId, std::string_view reason)> onHostDeviceLost;

  std::function<void(std::string_view pluginId, std::string_view reason)> onPluginClosed;

  std::function<void(std::string_view pluginId, const bitoscope::ipc::OpenProgress& progress)> onOpenProgress;

  std::function<void(uint64_t run, uint32_t device, const bitoscope::capture::CapturePlan& plan,
                      const std::optional<bitoscope::trigger::TriggerSpec>& trigger)>
      onPlanned;
  std::function<void(uint64_t run, uint32_t device, const bitoscope::acquisition::AcquireWarning& warning)>
      onWarning;
  std::function<void(uint64_t run, uint32_t device, std::string_view pluginId,
                      const std::vector<bitoscope::ipc::props::PropertyValue>& values)>
      onPropertiesSet;
  std::function<void(uint64_t run, uint32_t device, std::string_view pluginId, uint64_t captureId)> onStarted;
  std::function<void(uint64_t run, uint32_t device, uint64_t nowMs,
                      const bitoscope::acquisition::Progress& progress)>
      onProgress;
  std::function<void(uint64_t run, uint32_t device)> onWaitEnded;
  std::function<void(uint64_t run, uint32_t device, uint64_t peakSlotsInUse, uint32_t slotCount)> onRingPeak;
  std::function<void(uint64_t run, uint32_t device)> onStopped;
  std::function<void(uint64_t run, uint32_t device, const bitoscope::acquisition::AcquireRetry& retry)>
      onRetrying;
  std::function<void(uint64_t run, uint32_t device, const bitoscope::capture::CaptureSummary& summary,
                      const bitoscope::capture::CapturePlan& plan,
                      std::optional<uint64_t> triggerSampleIndex,
                      const std::optional<bitoscope::capture::MemoryShortfall>& shortfall)>
      onCompleted;

  std::function<void(uint64_t run,
                     const std::expected<bitoscope::acquisition::SessionResult, std::string>& result)>
      onCaptureFinished;

  std::function<void(uint64_t run, std::size_t decoderIndex,
                     const std::expected<std::shared_ptr<const bitoscope::gui_decode::DecodedAnnotations>,
                                         std::string>& result)>
      onDecoderResult;

  std::function<void(uint64_t run, std::size_t decoderIndex, const bitoscope::gui_decode::DecodeProgress& progress)>
      onDecoderProgress;
};

}
