/*
 * SPDX-License-Identifier: MIT
 */

#include "frame_probe.h"

#include <array>

namespace bitoscope::gui_frame_probe {

namespace {

constexpr std::array<std::string_view, kDrawPartCount> kPartNames{
  "prologue", "columns", "upload", "headers", "decoder-lanes", "dots-overlays", "popups"};

}

std::string_view drawPartName(DrawPart part) {
  const auto index = static_cast<std::size_t>(part);
  return index < kPartNames.size() ? kPartNames[index] : std::string_view("unknown");
}

FrameProbe::~FrameProbe() = default;

DrawPartScope::DrawPartScope(FrameProbe* probe, DrawPart part) : probe_(probe) {
  if (probe_ != nullptr) {
    probe_->enter(part);
  }
}

DrawPartScope::~DrawPartScope() {
  if (probe_ != nullptr) {
    probe_->leave();
  }
}

void DrawPartScope::next(DrawPart part) {
  if (probe_ != nullptr) {
    probe_->leave();
    probe_->enter(part);
  }
}

}
