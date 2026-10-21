/*
 * SPDX-License-Identifier: MIT
 */

#include "gui_event_queue.h"

#include <utility>

namespace bitoscope::gui_events {

void GuiEventQueue::setWakeCallback(std::function<void()> wake) {
  std::lock_guard<std::mutex> lock(mutex_);
  wake_ = std::move(wake);
}

void GuiEventQueue::post(Event event) {
  std::function<void()> wake;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    queue_.push_back(std::move(event));
    wake = wake_;
  }
  // WHY: Wake callback is called outside the lock: a slow or queue-touching callback must not block other posters.
  if (wake) {
    wake();
  }
}

std::size_t GuiEventQueue::drain(const GuiEventHandlers& handlers) {
  // WHY: Swap the queue out under the lock and call handlers outside it: a post() from a handler must not deadlock.
  std::deque<Event> taken;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    taken.swap(queue_);
  }
  for (Event& event : taken) {
    event(handlers);
  }
  return taken.size();
}

std::size_t GuiEventQueue::pendingCountForTest() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return queue_.size();
}

}
