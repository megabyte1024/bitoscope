/*
 * SPDX-License-Identifier: MIT
 */

#include "imgui_bridge.h"

#include <imgui_internal.h>

#include <cmath>

namespace bitoscope::gui_test_input {

namespace {

ImGuiKey offsetKey(ImGuiKey first, int offset) { return static_cast<ImGuiKey>(static_cast<int>(first) + offset); }

int distance(KeyId from, KeyId to) { return static_cast<int>(to) - static_cast<int>(from); }

bool inRange(KeyId key, KeyId first, KeyId last) {
  return static_cast<int>(key) >= static_cast<int>(first) && static_cast<int>(key) <= static_cast<int>(last);
}

RectPx rectOf(const ImGuiWindow& window, CoordinateScale scale) {
  const PointPx origin = toPhysical(window.Pos.x, window.Pos.y, scale);
  const PointPx size = toPhysical(window.Size.x, window.Size.y, scale);
  return RectPx{origin.x, origin.y, size.x, size.y};
}

bool isSameOrChildOf(const ImGuiWindow* inner, const ImGuiWindow* outer) {
  for (const ImGuiWindow* w = inner; w != nullptr; w = w->ParentWindow) {
    if (w == outer) {
      return true;
    }
  }
  return false;
}

ImGuiKey leftKeyOfModifier(KeyId key) {
  switch (key) {
  case KeyId::ModCtrl: return ImGuiKey_LeftCtrl;
  case KeyId::ModShift: return ImGuiKey_LeftShift;
  case KeyId::ModAlt: return ImGuiKey_LeftAlt;
  case KeyId::ModSuper: return ImGuiKey_LeftSuper;
  default: return ImGuiKey_None;
  }
}

}

ImGuiKey toImGuiKey(KeyId key) {
  if (inRange(key, KeyId::A, KeyId::Z)) {
    return offsetKey(ImGuiKey_A, distance(KeyId::A, key));
  }
  if (inRange(key, KeyId::Digit0, KeyId::Digit9)) {
    return offsetKey(ImGuiKey_0, distance(KeyId::Digit0, key));
  }
  if (inRange(key, KeyId::F1, KeyId::F12)) {
    return offsetKey(ImGuiKey_F1, distance(KeyId::F1, key));
  }
  if (inRange(key, KeyId::NumPad0, KeyId::NumPad9)) {
    return offsetKey(ImGuiKey_Keypad0, distance(KeyId::NumPad0, key));
  }
  switch (key) {
  case KeyId::Enter: return ImGuiKey_Enter;
  case KeyId::Escape: return ImGuiKey_Escape;
  case KeyId::Delete: return ImGuiKey_Delete;
  case KeyId::Backspace: return ImGuiKey_Backspace;
  case KeyId::Tab: return ImGuiKey_Tab;
  case KeyId::Space: return ImGuiKey_Space;
  case KeyId::Left: return ImGuiKey_LeftArrow;
  case KeyId::Right: return ImGuiKey_RightArrow;
  case KeyId::Up: return ImGuiKey_UpArrow;
  case KeyId::Down: return ImGuiKey_DownArrow;
  case KeyId::Home: return ImGuiKey_Home;
  case KeyId::End: return ImGuiKey_End;
  case KeyId::PageUp: return ImGuiKey_PageUp;
  case KeyId::PageDown: return ImGuiKey_PageDown;
  case KeyId::Insert: return ImGuiKey_Insert;
  case KeyId::Equal: return ImGuiKey_Equal;
  case KeyId::Minus: return ImGuiKey_Minus;
  case KeyId::ModCtrl: return ImGuiMod_Ctrl;
  case KeyId::ModShift: return ImGuiMod_Shift;
  case KeyId::ModAlt: return ImGuiMod_Alt;
  case KeyId::ModSuper: return ImGuiMod_Super;
  default: return ImGuiKey_None;
  }
}

void blockRealInput(ImGuiIO& io) { io.SetAppAcceptingEvents(false); }

void applyEvents(ImGuiIO& io, std::span<const InputEvent> events) {
  const bool before = io.AppAcceptingEvents;
  io.SetAppAcceptingEvents(true);
  for (const InputEvent& e : events) {
    switch (e.kind) {
    case InputEvent::Kind::MousePos:
      io.AddMousePosEvent(e.x, e.y);
      break;
    case InputEvent::Kind::MouseButton:
      io.AddMouseButtonEvent(static_cast<int>(e.button), e.down);
      break;
    case InputEvent::Kind::MouseWheel:
      io.AddMouseWheelEvent(e.x, e.y);
      break;
    case InputEvent::Kind::Key:
      io.AddKeyEvent(toImGuiKey(e.key), e.down);
      // WHY: Report a modifier as generic ImGuiMod_ and as left key, as the Win32 backend does; shortcuts read ImGuiMod_.
      if (const ImGuiKey left = leftKeyOfModifier(e.key); left != ImGuiKey_None) {
        io.AddKeyEvent(left, e.down);
      }
      break;
    case InputEvent::Kind::Text:
      io.AddInputCharactersUTF8(e.text.c_str());
      break;
    }
  }
  io.SetAppAcceptingEvents(before);
}

CoordinateScale scaleOf(const ImGuiIO& io) {
  return CoordinateScale{io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y};
}

std::vector<WindowInfo> collectWindows(ImGuiContext& context) {
  std::vector<WindowInfo> out;
  const CoordinateScale scale = scaleOf(context.IO);
  const ImGuiWindow* const navWindow = context.NavWindow;
  const ImGuiWindow* const hoveredWindow = context.HoveredWindow;
  for (const ImGuiWindow* window : context.Windows) {
    if (window == nullptr || !window->Active || window->Hidden) {
      continue;
    }
    WindowInfo info;
    info.name = window->Name != nullptr ? window->Name : "";
    info.rect = rectOf(*window, scale);
    info.focused = navWindow != nullptr && isSameOrChildOf(navWindow, window);
    info.hovered = hoveredWindow != nullptr && isSameOrChildOf(hoveredWindow, window);
    info.appearing = window->Appearing;
    info.child = (window->Flags & ImGuiWindowFlags_ChildWindow) != 0;
    info.tooltip = (window->Flags & ImGuiWindowFlags_Tooltip) != 0;
    info.popup = (window->Flags & (ImGuiWindowFlags_Popup | ImGuiWindowFlags_Modal | ImGuiWindowFlags_ChildMenu)) != 0;
    out.push_back(std::move(info));
  }
  return out;
}

HoverInfo collectHover(ImGuiContext& context) {
  HoverInfo hover;
  const ImGuiIO& io = context.IO;
  const CoordinateScale scale = scaleOf(io);
  hover.mouseValid = ImGui::IsMousePosValid(&io.MousePos);
  if (hover.mouseValid) {
    hover.mouse = toPhysical(io.MousePos.x, io.MousePos.y, scale);
  }
  if (context.HoveredWindow != nullptr && context.HoveredWindow->Name != nullptr) {
    hover.windowName = context.HoveredWindow->Name;
  }
  hover.hasItem = context.HoveredId != 0;
  hover.itemId = context.HoveredId;
  hover.wantCaptureMouse = io.WantCaptureMouse;
  hover.wantCaptureKeyboard = io.WantCaptureKeyboard;
  hover.wantTextInput = io.WantTextInput;
  hover.activeId = context.ActiveId;
  hover.mouseCursor = static_cast<std::int32_t>(context.MouseCursor);
  return hover;
}

InfoReply collectInfo(ImGuiContext& context) {
  const ImGuiIO& io = context.IO;
  const CoordinateScale scale = scaleOf(io);
  InfoReply info;
  const PointPx size = toPhysical(io.DisplaySize.x, io.DisplaySize.y, scale);
  info.width = size.x;
  info.height = size.y;
  info.scaleX = scale.x;
  info.scaleY = scale.y;
  info.frame = static_cast<std::uint64_t>(context.FrameCount > 0 ? context.FrameCount : 0);
  return info;
}

}
