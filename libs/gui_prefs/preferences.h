/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <device_identity.h>
#include <lane_order.h>
#include <number_format.h>
#include <properties_panel.h>

#include "theme.h"
#include "lane_name_width.h"
#include "open_session_files.h"
#include "picker_height.h"
#include "zoom_level.h"

namespace bitoscope::gui_prefs {

inline constexpr int32_t kCurrentFormatVersion = 2;

struct WindowPosition {
  int32_t x = 0;
  int32_t y = 0;

  bool operator==(const WindowPosition&) const = default;
};

// WHY: WindowPlacement units depend on its WindowSystem key (physical for win32/x11, logical for
// wayland/cocoa); never mix.
struct WindowPlacement {
  std::optional<WindowPosition> position;
  int32_t width = 0;
  int32_t height = 0;
  bool maximized = false;

  bool operator==(const WindowPlacement&) const = default;
};

enum class WindowSystem {
  Win32,
  X11,
  Wayland,
  Cocoa,
};

enum class PropertiesSide {
  Left,
  Right,
};

enum class FontChoice {
  System,
  GoogleSansFlex,
};

enum class SignalStyle {
  LevelColors,
  ChannelColors,
};

enum class UnsavedCapturePolicy {
  Ask,
  DontSave,
};

struct ToolPanels {
  bool logOpen = false;

  bool operator==(const ToolPanels&) const = default;
};

struct SessionPanelState {
  bool propertiesOpen = true;

  gui_session::PropertiesView propertiesView = gui_session::PropertiesView::Device;

  std::optional<int32_t> propertiesWidth;

  std::string device;

  std::optional<gui_session::DeviceIdentity> deviceIdentity{};

  std::optional<waveform_view::PinSide> lanePinSide{};

  bool operator==(const SessionPanelState&) const = default;
};

struct Preferences {
  ThemeMode themeMode = ThemeMode::System;

  std::string language;

  // WHY: std::map, not unordered_map, so serializePreferences() writes keys in a fixed order.
  std::map<WindowSystem, WindowPlacement> windowPlacements;

  ToolPanels toolPanels;

  PropertiesSide propertiesSide = PropertiesSide::Left;

  bool fullScreen = false;

  bool reduceIdleFrameRate = false;

  FontChoice fontChoice = FontChoice::System;

  SignalStyle signalStyle = SignalStyle::LevelColors;

  decode::HexStyle hexStyle = decode::HexStyle::Suffixed;

  bool showBits = true;

  waveform_view::PinSide lanePinSide = waveform_view::PinSide::Bottom;

  UnsavedCapturePolicy unsavedCapture = UnsavedCapturePolicy::Ask;

  std::vector<std::string> recentFiles;

  OpenSessionFiles openSessionFiles;

  std::string lastFileFolder;

  std::vector<std::string> sessionPanels{"Session 1"};

  std::map<std::string, SessionPanelState> sessionPanelStates;

  std::string dockingLayout;

  int32_t zoomPercent = kZoomDefaultPercent;

  int32_t laneNameWidthCentiEm = kLaneNameWidthDefaultCentiEm;

  int32_t decoderPickerHeightCentiEm = kPickerHeightDefaultCentiEm;

  uint64_t decoderMemoryBudgetMib = 0;

  int32_t formatVersion = kCurrentFormatVersion;

  bool operator==(const Preferences&) const = default;
};

}
