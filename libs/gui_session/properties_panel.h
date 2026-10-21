/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <optional>

namespace bitoscope::gui_session {

enum class PropertiesView {
  Device,
  Decoders,
};

class PropertiesPanel {
 public:
  PropertiesPanel() = default;

  PropertiesPanel(bool shown, PropertiesView view);

  bool isShown() const { return shown_; }

  std::optional<PropertiesView> shownView() const;

  PropertiesView selectedView() const { return view_; }

  void click(PropertiesView view);

 private:
  bool shown_ = true;
  PropertiesView view_ = PropertiesView::Device;
};

}
