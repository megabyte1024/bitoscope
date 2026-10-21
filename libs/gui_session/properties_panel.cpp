/*
 * SPDX-License-Identifier: MIT
 */

#include "properties_panel.h"

namespace bitoscope::gui_session {

PropertiesPanel::PropertiesPanel(bool shown, PropertiesView view) : shown_(shown), view_(view) {}

std::optional<PropertiesView> PropertiesPanel::shownView() const {
  if (!shown_) {
    return std::nullopt;
  }
  return view_;
}

void PropertiesPanel::click(PropertiesView view) {
  if (shown_ && view_ == view) {
    shown_ = false;
    return;
  }
  shown_ = true;
  view_ = view;
}

}
