/*
 * SPDX-License-Identifier: MIT
 */

#include "host_device_lost_adapter.h"

#include <string>

namespace bitoscope::gui_events {

std::function<void(std::string_view, std::string_view)> makePostingHostDeviceLost(
    GuiEventQueue& queue) {
  return [&queue](std::string_view pluginId, std::string_view reason) {
    std::string id(pluginId);
    std::string why(reason);
    queue.post([id, why](const GuiEventHandlers& handlers) {
      if (handlers.onHostDeviceLost) handlers.onHostDeviceLost(id, why);
    });
  };
}

std::function<void(std::string_view, std::string_view)> makePostingPluginClosed(GuiEventQueue& queue) {
  return [&queue](std::string_view pluginId, std::string_view reason) {
    std::string id(pluginId);
    std::string why(reason);
    queue.post([id, why](const GuiEventHandlers& handlers) {
      if (handlers.onPluginClosed) handlers.onPluginClosed(id, why);
    });
  };
}

std::function<void(std::string_view, const bitoscope::ipc::OpenProgress&)> makePostingOpenProgress(
    GuiEventQueue& queue) {
  return [&queue](std::string_view pluginId, const bitoscope::ipc::OpenProgress& progress) {
    std::string id(pluginId);
    queue.post([id, progress](const GuiEventHandlers& handlers) {
      if (handlers.onOpenProgress) handlers.onOpenProgress(id, progress);
    });
  };
}

}
