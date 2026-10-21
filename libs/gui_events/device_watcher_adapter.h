/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "device_watcher.h"
#include "gui_event_queue.h"

namespace bitoscope::gui_events {

bitoscope::host::DeviceWatcherEvents makePostingDeviceWatcherEvents(GuiEventQueue& queue,
                                                                    std::string driver);

}
