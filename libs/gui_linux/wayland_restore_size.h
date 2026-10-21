/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <optional>

namespace bitoscope::gui_linux {

struct WindowSize {
  int width = 0;
  int height = 0;

  bool operator==(const WindowSize&) const = default;
};

class WaylandRestoreSize {
 public:
  void remember(WindowSize normal);
  void windowRestored();
  bool resizePending() const;
  std::optional<WindowSize> takeResize();

 private:
  WindowSize remembered_;
  bool pending_ = false;
};

}
