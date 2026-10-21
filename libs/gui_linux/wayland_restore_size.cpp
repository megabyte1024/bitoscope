/*
 * SPDX-License-Identifier: MIT
 */

#include "wayland_restore_size.h"

namespace bitoscope::gui_linux {

void WaylandRestoreSize::remember(WindowSize normal) {
  remembered_ = normal;
  pending_ = false;
}

void WaylandRestoreSize::windowRestored() {
  pending_ = remembered_.width > 0 && remembered_.height > 0;
}

bool WaylandRestoreSize::resizePending() const {
  return pending_;
}

std::optional<WindowSize> WaylandRestoreSize::takeResize() {
  if (!pending_) {
    return std::nullopt;
  }
  pending_ = false;
  return remembered_;
}

}
