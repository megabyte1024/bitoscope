/*
 * SPDX-License-Identifier: MIT
 */

#include "preferences_json.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <climits>
#include <cstdint>
#include <format>
#include <optional>

#include "open_session_files.h"
#include "recent_files.h"
#include "lane_name_width.h"
#include "picker_height.h"
#include "zoom_level.h"

namespace bitoscope::gui_prefs {

namespace {

bool isJsonInteger(const nlohmann::json& value) {
  return value.is_number_integer() || value.is_number_unsigned();
}

std::string_view themeModeName(ThemeMode mode) {
  switch (mode) {
    case ThemeMode::System:
      return "System";
    case ThemeMode::Light:
      return "Light";
    case ThemeMode::Dark:
      return "Dark";
  }
  return "System";
}

std::string_view propertiesSideName(PropertiesSide side) {
  switch (side) {
    case PropertiesSide::Left:
      return "Left";
    case PropertiesSide::Right:
      return "Right";
  }
  return "Left";
}

std::string_view signalStyleName(SignalStyle style) {
  switch (style) {
    case SignalStyle::LevelColors:
      return "levelColors";
    case SignalStyle::ChannelColors:
      return "channelColors";
  }
  return "levelColors";
}

std::string_view hexStyleName(decode::HexStyle style) {
  switch (style) {
    case decode::HexStyle::Bare:
      return "bare";
    case decode::HexStyle::Prefixed:
      return "prefixed";
    case decode::HexStyle::Suffixed:
      return "suffixed";
  }
  return "suffixed";
}

std::string_view pinSideName(waveform_view::PinSide side) {
  return side == waveform_view::PinSide::Top ? "top" : "bottom";
}

std::string_view fontChoiceName(FontChoice choice) {
  switch (choice) {
    case FontChoice::System:
      return "System";
    case FontChoice::GoogleSansFlex:
      return "GoogleSansFlex";
  }
  return "System";
}

std::expected<WindowPlacement, std::string> parseWindowPlacement(const std::string& key,
                                                                 const nlohmann::json& wp,
                                                                 bool positionAllowed) {
  if (!wp.is_object()) {
    return std::unexpected(std::format("{}: must be object", key));
  }
  WindowPlacement placement;
  const auto integer = [&wp, &key](const char* name, int32_t& out) -> std::optional<std::string> {
    if (!wp.contains(name) || !isJsonInteger(wp.at(name))) {
      return std::format("{}.{}: must be integer", key, name);
    }
    out = wp.at(name).get<int32_t>();
    return std::nullopt;
  };

  const bool hasX = wp.contains("x");
  const bool hasY = wp.contains("y");
  if (hasX || hasY) {
    if (!positionAllowed) {
      return std::unexpected(
        std::format("{}: must not have position (this window system never reports one)", key));
    }
    if (!hasX || !hasY) {
      return std::unexpected(std::format("{}: must have both x and y or neither", key));
    }
    WindowPosition position;
    if (auto problem = integer("x", position.x)) {
      return std::unexpected(*problem);
    }
    if (auto problem = integer("y", position.y)) {
      return std::unexpected(*problem);
    }
    placement.position = position;
  }

  if (auto problem = integer("width", placement.width)) {
    return std::unexpected(*problem);
  }
  if (placement.width <= 0) {
    return std::unexpected(std::format("{}.width: must be positive", key));
  }
  if (auto problem = integer("height", placement.height)) {
    return std::unexpected(*problem);
  }
  if (placement.height <= 0) {
    return std::unexpected(std::format("{}.height: must be positive", key));
  }
  if (!wp.contains("maximized") || !wp.at("maximized").is_boolean()) {
    return std::unexpected(std::format("{}.maximized: must be boolean", key));
  }
  placement.maximized = wp.at("maximized").get<bool>();
  return placement;
}

std::optional<WindowSystem> parseWindowSystemKey(const std::string& key) {
  if (key == "win32") return WindowSystem::Win32;
  if (key == "x11") return WindowSystem::X11;
  if (key == "wayland") return WindowSystem::Wayland;
  if (key == "cocoa") return WindowSystem::Cocoa;
  return std::nullopt;
}

std::string_view windowSystemKey(WindowSystem system) {
  switch (system) {
    case WindowSystem::Win32:
      return "win32";
    case WindowSystem::X11:
      return "x11";
    case WindowSystem::Wayland:
      return "wayland";
    case WindowSystem::Cocoa:
      return "cocoa";
  }
  return "win32";
}

bool windowSystemPositionAllowed(WindowSystem system) {
  return system == WindowSystem::Win32 || system == WindowSystem::X11;
}

std::expected<SessionPanelState, std::string> parseSessionPanelState(
  const std::string& name, const nlohmann::json& entry, std::vector<std::string>* warnings) {
  const std::string field = "sessionPanelStates." + name;
  if (!entry.is_object()) {
    return std::unexpected(std::format("{}: must be object", field));
  }
  SessionPanelState state;
  if (entry.contains("propertiesOpen")) {
    if (!entry.at("propertiesOpen").is_boolean()) {
      return std::unexpected(std::format("{}.propertiesOpen: must be boolean", field));
    }
    state.propertiesOpen = entry.at("propertiesOpen").get<bool>();
  }
  // WHY: Saved "generator" is a view once written by this GUI and since removed: it loads silently as Device,
  // no warning.
  if (entry.contains("propertiesView")) {
    const auto& v = entry.at("propertiesView");
    if (!v.is_string()) {
      return std::unexpected(std::format("{}.propertiesView: must be string", field));
    }
    const std::string s = v.get<std::string>();
    if (s == "device") {
      state.propertiesView = gui_session::PropertiesView::Device;
    } else if (s == "decoders") {
      state.propertiesView = gui_session::PropertiesView::Decoders;
    } else if (s == "generator") {
      state.propertiesView = gui_session::PropertiesView::Device;
    } else if (warnings != nullptr) {
      warnings->push_back(
        std::format("{}.propertiesView: unknown value '{}', using device instead", field, s));
    }
  }
  if (entry.contains("lanePinSide")) {
    const auto& v = entry.at("lanePinSide");
    if (!v.is_string()) {
      return std::unexpected(std::format("{}.lanePinSide: must be string", field));
    }
    const std::string s = v.get<std::string>();
    if (s == "top") {
      state.lanePinSide = waveform_view::PinSide::Top;
    } else if (s == "bottom") {
      state.lanePinSide = waveform_view::PinSide::Bottom;
    } else if (warnings != nullptr) {
      warnings->push_back(std::format(
        "{}.lanePinSide: unknown value '{}', following GUI-wide side instead", field, s));
    }
  }
  if (entry.contains("propertiesWidth")) {
    const auto& width = entry.at("propertiesWidth");
    if (!isJsonInteger(width)) {
      return std::unexpected(std::format("{}.propertiesWidth: must be integer", field));
    }
    const bool tooLarge = width.is_number_unsigned() &&
                          width.get<std::uint64_t>() > static_cast<std::uint64_t>(INT32_MAX);
    if (tooLarge || width.get<int64_t>() <= 0 || width.get<int64_t>() > INT32_MAX) {
      return std::unexpected(
        std::format("{}.propertiesWidth: must be positive 32-bit integer", field));
    }
    state.propertiesWidth = static_cast<int32_t>(width.get<int64_t>());
  }
  if (entry.contains("device")) {
    if (!entry.at("device").is_string()) {
      return std::unexpected(std::format("{}.device: must be string", field));
    }
    state.device = entry.at("device").get<std::string>();
  }
  if (entry.contains("deviceIdentity")) {
    const std::string idField = field + ".deviceIdentity";
    const auto& identity = entry.at("deviceIdentity");
    if (!identity.is_object()) {
      return std::unexpected(std::format("{}: must be object", idField));
    }
    gui_session::DeviceIdentity device;
    for (auto [key, target] : {std::pair<const char*, std::string*>{"driver", &device.driver},
                               std::pair<const char*, std::string*>{"id", &device.id}}) {
      if (!identity.contains(key) || !identity.at(key).is_string() ||
          identity.at(key).get<std::string>().empty()) {
        return std::unexpected(std::format("{}.{}: must be non-empty string", idField, key));
      }
      *target = identity.at(key).get<std::string>();
    }
    if (identity.contains("serialNumber")) {
      if (!identity.at("serialNumber").is_string()) {
        return std::unexpected(std::format("{}.serialNumber: must be string", idField));
      }
      device.serialNumber = identity.at("serialNumber").get<std::string>();
    }
    state.deviceIdentity = std::move(device);
  }
  return state;
}

}

std::expected<Preferences, std::string> parsePreferences(std::string_view json,
                                                         std::vector<std::string>* warnings,
                                                         LegacyPlacementTarget legacyPlacementTarget) {
  if (json.empty()) {
    return std::unexpected(std::string("JSON: empty document"));
  }

  const nlohmann::json::parser_callback_t noCallback = nullptr;
  constexpr bool allowExceptions = false;
  const nlohmann::json root = nlohmann::json::parse(json, noCallback, allowExceptions);
  if (root.is_discarded()) {
    return std::unexpected(std::string("JSON: malformed document"));
  }
  if (!root.is_object()) {
    return std::unexpected(std::string("JSON: top level is not object"));
  }

  Preferences prefs;

  if (root.contains("formatVersion")) {
    const auto& v = root.at("formatVersion");
    if (!isJsonInteger(v)) {
      return std::unexpected(std::string("formatVersion: must be integer"));
    }
    const int64_t version = v.get<int64_t>();
    if (version < 0) {
      return std::unexpected(std::string("formatVersion: must not be negative"));
    }
    prefs.formatVersion = static_cast<int32_t>(version);
  }

  if (root.contains("themeMode")) {
    const auto& v = root.at("themeMode");
    if (!v.is_string()) {
      return std::unexpected(std::string("themeMode: must be string"));
    }
    const std::string s = v.get<std::string>();
    if (s == "System") {
      prefs.themeMode = ThemeMode::System;
    } else if (s == "Light") {
      prefs.themeMode = ThemeMode::Light;
    } else if (s == "Dark") {
      prefs.themeMode = ThemeMode::Dark;
    } else {
      return std::unexpected(std::format("themeMode: unknown value '{}'", s));
    }
  }

  if (root.contains("propertiesSide")) {
    const auto& v = root.at("propertiesSide");
    if (!v.is_string()) {
      return std::unexpected(std::string("propertiesSide: must be string"));
    }
    const std::string s = v.get<std::string>();
    if (s == "Left") {
      prefs.propertiesSide = PropertiesSide::Left;
    } else if (s == "Right") {
      prefs.propertiesSide = PropertiesSide::Right;
    } else {
      return std::unexpected(std::format("propertiesSide: unknown value '{}'", s));
    }
  }

  if (root.contains("fullScreen")) {
    const auto& v = root.at("fullScreen");
    if (!v.is_boolean()) {
      return std::unexpected(std::string("fullScreen: must be boolean"));
    }
    prefs.fullScreen = v.get<bool>();
  }

  if (root.contains("reduceIdleFrameRate")) {
    const auto& v = root.at("reduceIdleFrameRate");
    if (!v.is_boolean()) {
      return std::unexpected(std::string("reduceIdleFrameRate: must be boolean"));
    }
    prefs.reduceIdleFrameRate = v.get<bool>();
  }

  if (root.contains("fontChoice")) {
    const auto& v = root.at("fontChoice");
    if (!v.is_string()) {
      return std::unexpected(std::string("fontChoice: must be string"));
    }
    const std::string s = v.get<std::string>();
    if (s == "System") {
      prefs.fontChoice = FontChoice::System;
    } else if (s == "GoogleSansFlex") {
      prefs.fontChoice = FontChoice::GoogleSansFlex;
    } else {
      return std::unexpected(std::format("fontChoice: unknown value '{}'", s));
    }
  }

  if (root.contains("signalStyle")) {
    const auto& v = root.at("signalStyle");
    if (!v.is_string()) {
      return std::unexpected(std::string("signalStyle: must be string"));
    }
    const std::string s = v.get<std::string>();
    if (s == "levelColors") {
      prefs.signalStyle = SignalStyle::LevelColors;
    } else if (s == "channelColors") {
      prefs.signalStyle = SignalStyle::ChannelColors;
    } else if (warnings != nullptr) {
      warnings->push_back(
        std::format("signalStyle: unknown value '{}', using levelColors instead", s));
    }
  }

  if (root.contains("hexStyle")) {
    const auto& v = root.at("hexStyle");
    const std::string s = v.is_string() ? v.get<std::string>() : v.dump();
    if (s == "bare") {
      prefs.hexStyle = decode::HexStyle::Bare;
    } else if (s == "prefixed") {
      prefs.hexStyle = decode::HexStyle::Prefixed;
    } else if (s == "suffixed") {
      prefs.hexStyle = decode::HexStyle::Suffixed;
    } else if (warnings != nullptr) {
      warnings->push_back(std::format("hexStyle: unknown value '{}', using suffixed instead", s));
    }
  }
  if (root.contains("showBits")) {
    const auto& v = root.at("showBits");
    if (v.is_boolean()) {
      prefs.showBits = v.get<bool>();
    } else if (warnings != nullptr) {
      warnings->push_back(std::format("showBits: must be boolean, got {}, using true instead", v.dump()));
    }
  }

  if (root.contains("lanePinSide")) {
    const auto& v = root.at("lanePinSide");
    if (!v.is_string()) {
      return std::unexpected(std::string("lanePinSide: must be string"));
    }
    const std::string s = v.get<std::string>();
    if (s == "top") {
      prefs.lanePinSide = waveform_view::PinSide::Top;
    } else if (s == "bottom") {
      prefs.lanePinSide = waveform_view::PinSide::Bottom;
    } else if (warnings != nullptr) {
      warnings->push_back(std::format("lanePinSide: unknown value '{}', using bottom instead", s));
    }
  }

  if (root.contains("unsavedCapture")) {
    const auto& v = root.at("unsavedCapture");
    if (!v.is_string()) {
      return std::unexpected(std::string("unsavedCapture: must be string"));
    }
    const std::string s = v.get<std::string>();
    if (s == "ask") {
      prefs.unsavedCapture = UnsavedCapturePolicy::Ask;
    } else if (s == "dontSave") {
      prefs.unsavedCapture = UnsavedCapturePolicy::DontSave;
    } else if (warnings != nullptr) {
      warnings->push_back(std::format("unsavedCapture: unknown value '{}', asking instead", s));
    }
  }

  if (root.contains("recentFiles")) {
    const auto& v = root.at("recentFiles");
    if (!v.is_array()) {
      if (warnings != nullptr) {
        warnings->push_back("recentFiles: must be array of strings; recent files list starts empty");
      }
    } else {
      std::vector<std::string> files;
      bool dropped = false;
      for (const auto& item : v) {
        if (item.is_string()) {
          files.push_back(item.get<std::string>());
        } else {
          dropped = true;
        }
      }
      if (dropped && warnings != nullptr) {
        warnings->push_back("recentFiles: entry that is not string was dropped");
      }
      prefs.recentFiles = normalizedRecentFiles(std::move(files));
    }
  }

  // WHY: An entry that is not a string is dropped but still counts for the index, so the selection stays on its file.
  if (root.contains("openSessionFiles")) {
    const auto& v = root.at("openSessionFiles");
    if (!v.is_array()) {
      if (warnings != nullptr) {
        warnings->push_back("openSessionFiles: must be array of strings; no session files are reopened");
      }
    } else {
      OpenSessionFiles read;
      bool dropped = false;
      for (const auto& item : v) {
        if (item.is_string()) {
          read.files.push_back(item.get<std::string>());
        } else {
          read.files.emplace_back();
          dropped = true;
        }
      }
      if (dropped && warnings != nullptr) {
        warnings->push_back("openSessionFiles: entry that is not string was dropped");
      }
      if (root.contains("selectedSessionFile")) {
        const auto& s = root.at("selectedSessionFile");
        if (!isJsonInteger(s)) {
          if (warnings != nullptr) {
            warnings->push_back("selectedSessionFile: must be integer; no session file is selected");
          }
        } else if (s.get<int64_t>() < 0 || s.get<int64_t>() >= static_cast<int64_t>(read.files.size())) {
          if (warnings != nullptr) {
            warnings->push_back(std::format("selectedSessionFile: {} is out of range; no session file is selected",
                                            s.get<int64_t>()));
          }
        } else {
          read.selected = static_cast<std::size_t>(s.get<int64_t>());
        }
      }
      prefs.openSessionFiles = normalizedOpenSessionFiles(std::move(read));
    }
  }

  if (root.contains("lastFileFolder")) {
    const auto& v = root.at("lastFileFolder");
    if (v.is_string()) {
      prefs.lastFileFolder = v.get<std::string>();
    } else if (warnings != nullptr) {
      warnings->push_back("lastFileFolder: must be string; file dialogs start in their own default folder");
    }
  }

  if (root.contains("language")) {
    const auto& v = root.at("language");
    if (!v.is_string()) {
      return std::unexpected(std::string("language: must be string"));
    }
    prefs.language = v.get<std::string>();
  }

  // WHY: Process the legacy record before windowPlacements so an explicit entry there for the same key wins.
  if (root.contains("windowPlacement")) {
    constexpr bool positionAllowed = true;
    auto placement = parseWindowPlacement("windowPlacement", root.at("windowPlacement"), positionAllowed);
    if (!placement) {
      if (warnings != nullptr) {
        warnings->push_back(
          std::format("{}; window opens at its default size and place", placement.error()));
      }
    } else if (legacyPlacementTarget == LegacyPlacementTarget::MapToWin32) {
      prefs.windowPlacements[WindowSystem::Win32] = *placement;
    } else if (warnings != nullptr) {
      warnings->push_back(
        "windowPlacement: dropped (older file's window placement does not say which window "
        "system it belongs to); window opens at its default size and place");
    }
  }

  if (root.contains("windowPlacements")) {
    const auto& wps = root.at("windowPlacements");
    if (!wps.is_object()) {
      if (warnings != nullptr) {
        warnings->push_back(std::string(
          "windowPlacements: must be object; every window system's saved placement is "
          "dropped"));
      }
    } else {
      for (const auto& [key, entry] : wps.items()) {
        const std::optional<WindowSystem> system = parseWindowSystemKey(key);
        if (!system.has_value()) {
          continue;
        }
        const std::string field = "windowPlacements." + key;
        auto placement =
          parseWindowPlacement(field, entry, windowSystemPositionAllowed(*system));
        if (placement) {
          prefs.windowPlacements[*system] = *placement;
        } else if (warnings != nullptr) {
          warnings->push_back(
            std::format("{}; that window system's saved placement is dropped", placement.error()));
        }
      }
    }
  }

  if (root.contains("toolPanels")) {
    const auto& tp = root.at("toolPanels");
    if (!tp.is_object()) {
      return std::unexpected(std::string("toolPanels: must be object"));
    }
    if (tp.contains("logOpen")) {
      if (!tp.at("logOpen").is_boolean()) {
        return std::unexpected(std::string("toolPanels.logOpen: must be boolean"));
      }
      prefs.toolPanels.logOpen = tp.at("logOpen").get<bool>();
    }
  }

  if (root.contains("sessionPanels")) {
    const auto& sp = root.at("sessionPanels");
    if (!sp.is_array()) {
      return std::unexpected(std::string("sessionPanels: must be array of strings"));
    }
    std::vector<std::string> panels;
    panels.reserve(sp.size());
    for (const auto& item : sp) {
      if (!item.is_string()) {
        return std::unexpected(std::string("sessionPanels: must be array of strings"));
      }
      panels.push_back(item.get<std::string>());
    }
    prefs.sessionPanels = std::move(panels);
  }

  if (root.contains("sessionPanelStates")) {
    const auto& states = root.at("sessionPanelStates");
    if (!states.is_object()) {
      return std::unexpected(std::string("sessionPanelStates: must be object"));
    }
    for (const auto& [name, entry] : states.items()) {
      auto state = parseSessionPanelState(name, entry, warnings);
      if (!state) {
        return std::unexpected(state.error());
      }
      prefs.sessionPanelStates.emplace(name, std::move(*state));
    }
  }

  if (root.contains("zoomPercent")) {
    const auto& v = root.at("zoomPercent");
    if (!isJsonInteger(v)) {
      return std::unexpected(std::string("zoomPercent: must be integer"));
    }
    const int64_t raw = v.get<int64_t>();
    const int32_t clampedRaw =
        static_cast<int32_t>((std::min)(int64_t{INT32_MAX}, (std::max)(int64_t{INT32_MIN}, raw)));
    const int32_t snapped = clampZoomPercent(clampedRaw);
    if (snapped != raw && warnings != nullptr) {
      warnings->push_back(std::format(
        "zoomPercent: {} is out of range or not on 10% step, using {} instead", raw, snapped));
    }
    prefs.zoomPercent = snapped;
  }

  if (root.contains("laneNameWidthCentiEm")) {
    const auto& v = root.at("laneNameWidthCentiEm");
    if (!isJsonInteger(v)) {
      if (warnings != nullptr) {
        warnings->push_back("laneNameWidthCentiEm: must be integer; lane names take default width");
      }
    } else {
      const int64_t raw = v.get<int64_t>();
      prefs.laneNameWidthCentiEm = clampLaneNameWidth(raw);
      if (prefs.laneNameWidthCentiEm != raw && warnings != nullptr) {
        warnings->push_back(std::format("laneNameWidthCentiEm: {} is out of range, using {} instead", raw,
                                        prefs.laneNameWidthCentiEm));
      }
    }
  }

  if (root.contains("decoderPickerHeightCentiEm")) {
    const auto& v = root.at("decoderPickerHeightCentiEm");
    if (!isJsonInteger(v)) {
      if (warnings != nullptr) {
        warnings->push_back("decoderPickerHeightCentiEm: must be integer; decoder list takes default height");
      }
    } else {
      const int64_t raw = v.get<int64_t>();
      prefs.decoderPickerHeightCentiEm = clampPickerHeightCentiEm(raw);
      if (prefs.decoderPickerHeightCentiEm != raw && warnings != nullptr) {
        warnings->push_back(std::format("decoderPickerHeightCentiEm: {} is out of range, using {} instead", raw,
                                        prefs.decoderPickerHeightCentiEm));
      }
    }
  }

  if (root.contains("decoderMemoryBudgetMib")) {
    const auto& v = root.at("decoderMemoryBudgetMib");
    if (!isJsonInteger(v) || v.get<int64_t>() < 0) {
      if (warnings != nullptr) {
        warnings->push_back("decoderMemoryBudgetMib: must be whole number of MiB, 0 or more; budget stays automatic");
      }
    } else {
      prefs.decoderMemoryBudgetMib = static_cast<uint64_t>(v.get<int64_t>());
    }
  }

  if (root.contains("dockingLayout")) {
    const auto& v = root.at("dockingLayout");
    if (!v.is_string()) {
      return std::unexpected(std::string("dockingLayout: must be string"));
    }
    prefs.dockingLayout = v.get<std::string>();
  }

  return prefs;
}

std::string serializePreferences(const Preferences& preferences) {
  nlohmann::json root;

  root["formatVersion"] = preferences.formatVersion;
  root["themeMode"] = std::string(themeModeName(preferences.themeMode));
  root["propertiesSide"] = std::string(propertiesSideName(preferences.propertiesSide));
  root["fullScreen"] = preferences.fullScreen;
  root["reduceIdleFrameRate"] = preferences.reduceIdleFrameRate;
  root["fontChoice"] = std::string(fontChoiceName(preferences.fontChoice));
  root["signalStyle"] = std::string(signalStyleName(preferences.signalStyle));
  root["hexStyle"] = std::string(hexStyleName(preferences.hexStyle));
  root["showBits"] = preferences.showBits;
  root["lanePinSide"] = std::string(pinSideName(preferences.lanePinSide));
  root["unsavedCapture"] = preferences.unsavedCapture == UnsavedCapturePolicy::DontSave ? "dontSave" : "ask";
  root["recentFiles"] = preferences.recentFiles;
  root["openSessionFiles"] = preferences.openSessionFiles.files;
  if (preferences.openSessionFiles.selected.has_value()) {
    root["selectedSessionFile"] = *preferences.openSessionFiles.selected;
  }
  root["lastFileFolder"] = preferences.lastFileFolder;
  root["language"] = preferences.language;

  if (!preferences.windowPlacements.empty()) {
    nlohmann::json placements = nlohmann::json::object();
    for (const auto& [system, p] : preferences.windowPlacements) {
      nlohmann::json entry = {{"width", p.width}, {"height", p.height}, {"maximized", p.maximized}};
      if (p.position.has_value()) {
        entry["x"] = p.position->x;
        entry["y"] = p.position->y;
      }
      placements[std::string(windowSystemKey(system))] = std::move(entry);
    }
    root["windowPlacements"] = std::move(placements);
  }

  root["toolPanels"] = {
      {"logOpen", preferences.toolPanels.logOpen},
  };

  root["sessionPanels"] = preferences.sessionPanels;
  nlohmann::json states = nlohmann::json::object();
  for (const auto& [name, state] : preferences.sessionPanelStates) {
    std::string propertiesView = "device";
    if (state.propertiesView == gui_session::PropertiesView::Decoders) {
      propertiesView = "decoders";
    }
    nlohmann::json entry = {
        {"propertiesOpen", state.propertiesOpen},
        {"propertiesView", propertiesView},
        {"device", state.device}};
    if (state.propertiesWidth.has_value()) {
      entry["propertiesWidth"] = *state.propertiesWidth;
    }
    if (state.lanePinSide.has_value()) {
      entry["lanePinSide"] = std::string(pinSideName(*state.lanePinSide));
    }
    if (state.deviceIdentity.has_value()) {
      entry["deviceIdentity"] = {{"driver", state.deviceIdentity->driver},
                                 {"id", state.deviceIdentity->id},
                                 {"serialNumber", state.deviceIdentity->serialNumber}};
    }
    states[name] = std::move(entry);
  }
  root["sessionPanelStates"] = std::move(states);
  root["dockingLayout"] = preferences.dockingLayout;
  root["zoomPercent"] = preferences.zoomPercent;
  root["laneNameWidthCentiEm"] = preferences.laneNameWidthCentiEm;
  root["decoderPickerHeightCentiEm"] = preferences.decoderPickerHeightCentiEm;
  root["decoderMemoryBudgetMib"] = preferences.decoderMemoryBudgetMib;

  return root.dump();
}

}
