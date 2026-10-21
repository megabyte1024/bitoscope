/*
 * SPDX-License-Identifier: MIT
 */

#include "device_watcher_adapter.h"

#include <string>
#include <utility>

namespace bitoscope::gui_events {

bitoscope::host::DeviceWatcherEvents makePostingDeviceWatcherEvents(GuiEventQueue& queue,
                                                                    std::string driver) {
  const std::string name = std::move(driver);
  bitoscope::host::DeviceWatcherEvents events;

  events.onDeviceAppeared = [&queue, name](const bitoscope::ipc::DeviceInfo& device) {
    queue.post([name, device](const GuiEventHandlers& handlers) {
      if (handlers.onDeviceAppeared) handlers.onDeviceAppeared(name, device);
    });
  };

  events.onDeviceLost = [&queue, name](std::string_view deviceId, std::string_view reason) {
    std::string id(deviceId);
    std::string why(reason);
    queue.post([name, id, why](const GuiEventHandlers& handlers) {
      if (handlers.onDeviceLost) handlers.onDeviceLost(name, id, why);
    });
  };

  events.onEnded = [&queue, name](const bitoscope::host::WatchEnd& end) {
    std::string why = end.cause;
    queue.post([name, why](const GuiEventHandlers& handlers) {
      if (handlers.onWatchEnded) handlers.onWatchEnded(name, why);
    });
  };

  return events;
}

}
