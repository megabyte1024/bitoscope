/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

// WHY: Queue is deliberately UNBOUNDED: a bound would drop events or block posters, and no event may be lost.

#include <cstddef>
#include <deque>
#include <functional>
#include <mutex>

#include "gui_event_handlers.h"

namespace bitoscope::gui_events {

class GuiEventQueue {
 public:
  using Event = std::function<void(const GuiEventHandlers& handlers)>;

  void post(Event event);

  std::size_t drain(const GuiEventHandlers& handlers);

  void setWakeCallback(std::function<void()> wake);

  std::size_t pendingCountForTest() const;

 private:
  mutable std::mutex mutex_;
  std::deque<Event> queue_;
  std::function<void()> wake_;
};

}
