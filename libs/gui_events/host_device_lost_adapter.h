/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <functional>
#include <string_view>

#include "gui_event_queue.h"
#include "ipc_messages.h"

namespace bitoscope::gui_events {

std::function<void(std::string_view pluginId, std::string_view reason)> makePostingHostDeviceLost(
    GuiEventQueue& queue);

std::function<void(std::string_view pluginId, std::string_view reason)> makePostingPluginClosed(
    GuiEventQueue& queue);

std::function<void(std::string_view pluginId, const bitoscope::ipc::OpenProgress& progress)> makePostingOpenProgress(
    GuiEventQueue& queue);

}
