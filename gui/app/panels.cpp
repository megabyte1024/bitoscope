/*
 * SPDX-License-Identifier: MIT
 */

#include <imgui.h>
#include <imgui_internal.h>

#include <channel_colors.h>
#include <capture_settings.h>
#include <channel_selection.h>
#include <default_channel_colors.h>
#include <display_format.h>
#include <device_label.h>
#include <device_status.h>
#include <decoder_versions.h>
#include <driver_version.h>
#include <driver_watch_mark.h>
#include <icon_size.h>
#include <lane_name_fit.h>
#include <log_scroll.h>
#include <new_session_wizard.h>
#include <popup.h>
#include <pipe_device_host.h>
#include <property_editor_model.h>
#include <text_field.h>
#include <property_tree_layout.h>
#include <version_format.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <exception>
#include <expected>
#include <format>
#include <limits>
#include <string>
#include <vector>

#include "gui_app.h"
#include "platform_services.h"
#include "session_waveform.h"
#include "tr.h"

namespace bitoscope::gui {

namespace gp = gui_properties;

namespace {

constexpr const char* kDockSpace = "bitoscope.dockspace";
constexpr const char* kLogId = "log";
constexpr const char* kWizardId = "new_session_wizard";

constexpr float kPropertiesWidthDefault = 320.0f;
constexpr float kPropertiesWidthMin = 160.0f;
constexpr float kSignalsWidthMin = 120.0f;
constexpr float kSplitterWidth = 6.0f;

constexpr float kChannelHeaderEm = 7.0f;

struct ShortcutRow {
  const char* shortcut;
  const char* action;
};

std::string sessionWindowTitle(const gui_session::Session& session) {
  return withId(session.name.c_str(), ("session/" + session.name).c_str());
}

void tooltip(const char* text) {
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled)) {
    ImGui::SetTooltip("%s", text);
  }
}

std::string driverVersionLine(std::string_view driver, std::string_view version) {
  return version.empty() ? trf("Driver {}, no version given", driver) : trf("Driver {}, version {}", driver, version);
}

void versionRow(const std::string& name, const std::string& version) {
  ImGui::BulletText("%s", name.c_str());
  ImGui::SameLine();
  ImGui::TextDisabled("%s", version.empty() ? tr("no version given") : version.c_str());
}

bool comboOf(const char* label, int& index, const std::vector<std::string>& items) {
  bool changed = false;
  index = std::clamp(index, 0, static_cast<int>(items.size()) - 1);
  if (ImGui::BeginCombo(label, items[static_cast<std::size_t>(index)].c_str())) {
    for (int i = 0; i < static_cast<int>(items.size()); ++i) {
      const bool selected = i == index;
      if (ImGui::Selectable(items[static_cast<std::size_t>(i)].c_str(), selected)) {
        index = i;
        changed = true;
      }
      if (selected) {
        ImGui::SetItemDefaultFocus();
      }
    }
    ImGui::EndCombo();
  }
  return changed;
}

float propertyControlWidthMin() { return ImGui::GetFontSize() * 6.0f; }

gui_format::PropertyTreeColumns propertyColumnsFor(const std::vector<std::string>& labels) {
  std::vector<float> widths;
  widths.reserve(labels.size());
  for (const std::string& label : labels) {
    widths.push_back(ImGui::CalcTextSize(label.c_str()).x);
  }
  return gui_format::propertyTreeColumns(widths, ImGui::GetStyle().ItemSpacing.x,
                                         ImGui::GetContentRegionAvail().x, propertyControlWidthMin());
}

void propertyRowLabel(const char* label, gui_format::PropertyTreeColumns columns, const char* help = nullptr) {
  const float rowStartX = ImGui::GetCursorPosX();
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted(label);
  if (help != nullptr && help[0] != '\0') {
    tooltip(help);
  }
  ImGui::SameLine(rowStartX + columns.label);
  ImGui::SetNextItemWidth(columns.control);
}

std::string driverStatus(const gui_devices::DriverEntry& entry) {
  switch (entry.state) {
  case gui_devices::ListingState::Listing:
    return tr("Looking for devices...");
  case gui_devices::ListingState::Failed:
    return trf("Could not list devices: {}", entry.problem);
  case gui_devices::ListingState::Listed:
    break;
  }
  const auto n = static_cast<unsigned long>(entry.devices.size());
  const std::string count = trfn("{} device found", "{} devices found", n, n);
  return gui_devices::driverWatchMark(entry).mark == gui_devices::WatchMark::Watched
           ? trf("{}; plug-and-play: follows devices plugged in and out", count)
           : count;
}

std::string reasonWordsText(gui_devices::ReasonWords words) {
  switch (words) {
  case gui_devices::ReasonWords::None:
    break;
  case gui_devices::ReasonWords::Busy:
    return tr("in use by another program");
  case gui_devices::ReasonWords::NoPermission:
    return tr("no permission");
  case gui_devices::ReasonWords::FirmwareLoading:
    return tr("firmware loading");
  case gui_devices::ReasonWords::Other:
    return tr("not available");
  case gui_devices::ReasonWords::WaitingForDevice:
    return tr("waiting for device to come back");
  }
  return {};
}

std::string reasonWithDetail(gui_devices::ReasonWords words, const std::string& detail) {
  const std::string text = reasonWordsText(words);
  if (text.empty() || detail.empty()) {
    return text.empty() ? detail : text;
  }
  return trf("{}: {}", text, detail);
}

std::string baudRateText(std::uint32_t baudRate) {
  return trf("{} baud", gui_format::withThousands(baudRate));
}

std::string timeOfDay(std::chrono::system_clock::time_point when) {
  const auto seconds = std::chrono::floor<std::chrono::seconds>(when);
  try {
    return std::format("{:%H:%M:%S}", std::chrono::zoned_time(std::chrono::current_zone(), seconds));
  } catch (const std::exception&) {
    return std::format("{:%H:%M:%S}", seconds);
  }
}

const char* levelLabel(log::LogLevel level) {
  switch (level) {
  case log::LogLevel::debug:
    return tr("debug");
  case log::LogLevel::warning:
    return tr("warning");
  case log::LogLevel::error:
    return tr("error");
  case log::LogLevel::info:
  case log::LogLevel::unspecified:
    break;
  }
  return tr("info");
}

ImVec4 selectedButtonFill() {
  return ImGui::GetStyleColorVec4(ImGuiCol_Header);
}

}

std::uint32_t GuiApp::iconTexturePixels(std::uint32_t layoutPixels) const {
  return gui_icons::iconRasterPixels(layoutPixels, ImGui::GetIO().DisplayFramebufferScale.x);
}

bool GuiApp::iconButton(const char* id, Icon icon, const char* tip, bool mirrored, bool selected) {
  return iconButtonSized(id, icon, iconPixelSize(), tip, mirrored, selected);
}

bool GuiApp::iconButtonSized(const char* id, Icon icon, std::uint32_t pixelSize, const char* tip,
                             bool mirrored, bool selected) {
  const float edge = static_cast<float>(pixelSize);
  if (selected) {
    ImGui::PushStyleColor(ImGuiCol_Button, selectedButtonFill());
  }
  const auto texture =
    icons_.get(static_cast<std::size_t>(icon), iconTexturePixels(pixelSize), iconColor());
  bool pressed = false;
  if (texture != gui_icons::kNoTexture) {
    const ImVec2 uv0 = mirrored ? ImVec2(1, 0) : ImVec2(0, 0);
    const ImVec2 uv1 = mirrored ? ImVec2(0, 1) : ImVec2(1, 1);
    pressed = ImGui::ImageButton(id, ImTextureRef(static_cast<ImTextureID>(texture)),
                                 ImVec2(edge, edge), uv0, uv1);
  } else {
    const ImVec2 padding = ImGui::GetStyle().FramePadding;
    ImGui::PushID(id);
    pressed = ImGui::Button("?", ImVec2(edge + 2 * padding.x, edge + 2 * padding.y));
    ImGui::PopID();
  }
  if (selected) {
    ImGui::PopStyleColor();
  }
  tooltip(tip);
  return pressed;
}

bool GuiApp::iconTextButton(const char* id, Icon icon, const char* label) {
  const std::uint32_t size = iconPixelSize();
  const float edge = static_cast<float>(size);
  const ImGuiStyle& style = ImGui::GetStyle();
  const ImVec2 text = ImGui::CalcTextSize(label);
  const ImVec2 button(style.FramePadding.x * 2 + edge + style.ItemInnerSpacing.x + text.x,
                      style.FramePadding.y * 2 + (std::max)(edge, text.y));
  ImGui::PushID(id);
  const bool pressed = ImGui::Button("##button", button);
  ImGui::PopID();
  const ImVec2 at = ImGui::GetItemRectMin();
  ImDrawList* draw = ImGui::GetWindowDrawList();
  const auto texture =
    icons_.get(static_cast<std::size_t>(icon), iconTexturePixels(size), iconColor());
  const float iconTop = at.y + (button.y - edge) * 0.5f;
  const float iconLeft = at.x + style.FramePadding.x;
  if (texture != gui_icons::kNoTexture) {
    draw->AddImage(ImTextureRef(static_cast<ImTextureID>(texture)), ImVec2(iconLeft, iconTop),
                   ImVec2(iconLeft + edge, iconTop + edge), ImVec2(0, 0), ImVec2(1, 1),
                   ImGui::GetColorU32(ImVec4(1, 1, 1, 1)));
  }
  draw->AddText(ImVec2(iconLeft + edge + style.ItemInnerSpacing.x, at.y + (button.y - text.y) * 0.5f),
                ImGui::GetColorU32(ImGuiCol_Text), label);
  return pressed;
}

static bool beginTopLevelMenu(const std::string& label) {
  ImGui::PushStyleColor(ImGuiCol_Header, ImGui::GetStyleColorVec4(ImGuiCol_HeaderHovered));
  ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImGui::GetStyleColorVec4(ImGuiCol_HeaderHovered));
  const bool open = ImGui::BeginMenu(label.c_str());
  ImGui::PopStyleColor(2);
  return open;
}

void GuiApp::drawMenuBar() {
  if (!ImGui::BeginMainMenuBar()) {
    return;
  }
  if (beginTopLevelMenu(withId(tr("File"), "menu/file"))) {
    if (ImGui::MenuItem(tr("New session..."))) {
      openNewSessionWizard();
    }
    if (ImGui::MenuItem(tr("Open..."))) {
      requestOpen();
    }
    if (ImGui::BeginMenu(withId(tr("Open recent"), "menu/open_recent").c_str())) {
      drawOpenRecentMenu();
      ImGui::EndMenu();
    }
    const auto activeSession = sessions_.active();
    const auto saveBlocked = activeSession ? sessionUi_[*activeSession].capture.saveBlocked()
                                           : std::optional(gui_capture::PanelCapture::SaveBlocked::NoCapture);
    if (ImGui::MenuItem(tr("Save"), nullptr, false, !saveBlocked)) {
      requestSave(*activeSession, false, gui_session_file::SaveCommand::Save);
    }
    if (saveBlocked) {
      tooltip(saveBlockedText(*saveBlocked).c_str());
    }
    if (ImGui::MenuItem(tr("Save as..."), nullptr, false, !saveBlocked)) {
      requestSave(*activeSession, false, gui_session_file::SaveCommand::SaveAs);
    }
    if (saveBlocked) {
      tooltip(saveBlockedText(*saveBlocked).c_str());
    }
    ImGui::Separator();
    if (ImGui::MenuItem(tr("Exit"))) {
      platform_.requestClose();
    }
    ImGui::EndMenu();
  }
  if (beginTopLevelMenu(withId(tr("View"), "menu/view"))) {
    if (ImGui::MenuItem(tr("Full screen"), "F11", platform_.isFullScreen())) {
      toggleFullScreen();
    }
    if (ImGui::MenuItem(tr("Save CPU when idle"), nullptr, reduceIdleFrameRate_)) {
      setReduceIdleFrameRate(!reduceIdleFrameRate_);
    }
    tooltip(tr("Draws fewer frames while nothing moves; turn off for smoothest response"));
    ImGui::Separator();
    if (ImGui::MenuItem(tr("Log"), nullptr, &toolPanels_.logOpen) && toolPanels_.logOpen) {
      dockLogAtBottom_ = true;
    }
    ImGui::Separator();
    if (ImGui::BeginMenu(withId(tr("Theme"), "menu/theme").c_str())) {
      using gui_prefs::ThemeMode;
      if (ImGui::MenuItem(trc("theme", "System"), nullptr, themeMode_ == ThemeMode::System)) {
        setThemeMode(ThemeMode::System);
      }
      if (ImGui::MenuItem(tr("Light"), nullptr, themeMode_ == ThemeMode::Light)) {
        setThemeMode(ThemeMode::Light);
      }
      if (ImGui::MenuItem(tr("Dark"), nullptr, themeMode_ == ThemeMode::Dark)) {
        setThemeMode(ThemeMode::Dark);
      }
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu(withId(tr("Font"), "menu/font").c_str())) {
      using gui_prefs::FontChoice;
      if (ImGui::MenuItem(trc("font", "System"), nullptr, fontChoice_ == FontChoice::System)) {
        chooseFontChoice(FontChoice::System);
      }
      if (ImGui::MenuItem("Google Sans Flex", nullptr,
                          fontChoice_ == FontChoice::GoogleSansFlex)) {
        chooseFontChoice(FontChoice::GoogleSansFlex);
      }
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu(withId(tr("Signal style"), "menu/signal_style").c_str())) {
      using gui_prefs::SignalStyle;
      if (ImGui::MenuItem(tr("Level colors"), nullptr, signalStyle_ == SignalStyle::LevelColors)) {
        setSignalStyle(SignalStyle::LevelColors);
      }
      if (ImGui::MenuItem(tr("Channel colors"), nullptr, signalStyle_ == SignalStyle::ChannelColors)) {
        setSignalStyle(SignalStyle::ChannelColors);
      }
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu(withId(tr("Hex number format"), "menu/hex_number_format").c_str())) {
      using decode::HexStyle;
      if (ImGui::MenuItem("FF", nullptr, hexStyle_ == HexStyle::Bare)) {
        setHexStyle(HexStyle::Bare);
      }
      if (ImGui::MenuItem("0xFF", nullptr, hexStyle_ == HexStyle::Prefixed)) {
        setHexStyle(HexStyle::Prefixed);
      }
      if (ImGui::MenuItem("FFh", nullptr, hexStyle_ == HexStyle::Suffixed)) {
        setHexStyle(HexStyle::Suffixed);
      }
      ImGui::EndMenu();
    }
    if (ImGui::MenuItem(tr("Show bits"), nullptr, showDerivedRows_)) {
      setShowBits(!showDerivedRows_);
    }
    if (ImGui::BeginMenu(withId(tr("Properties position"), "menu/properties_position").c_str())) {
      using gui_prefs::PropertiesSide;
      if (ImGui::MenuItem(trc("properties_position", "Left"), nullptr,
                          propertiesSide_ == PropertiesSide::Left)) {
        setPropertiesSide(PropertiesSide::Left);
      }
      if (ImGui::MenuItem(trc("properties_position", "Right"), nullptr,
                          propertiesSide_ == PropertiesSide::Right)) {
        setPropertiesSide(PropertiesSide::Right);
      }
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu(withId(tr("Language"), "menu/language").c_str())) {
      if (ImGui::MenuItem(trc("language", "System"), nullptr, languagePreference_.empty())) {
        chooseLanguage("");
      }
      if (ImGui::MenuItem("English", nullptr, languagePreference_ == "en")) {
        chooseLanguage("en");
      }
      for (const std::string& catalog : catalogs_) {
        if (ImGui::MenuItem(catalog.c_str(), nullptr, languagePreference_ == catalog)) {
          chooseLanguage(catalog);
        }
      }
      ImGui::EndMenu();
    }
    ImGui::Separator();
    if (ImGui::MenuItem(tr("Zoom In"), tr("Ctrl+="))) {
      zoomIn();
    }
    if (ImGui::MenuItem(tr("Zoom Out"), tr("Ctrl+-"))) {
      zoomOut();
    }
    if (ImGui::MenuItem(tr("Reset Zoom"), tr("Ctrl+NumPad0"))) {
      resetZoom();
    }
    ImGui::Separator();
    if (ImGui::MenuItem(tr("Reset layout"))) {
      resetLayout_ = true;
      toolPanels_ = {};
      logger_.log(log::LogLevel::info, "gui", "Layout reset to default");
    }
    ImGui::EndMenu();
  }
  if (beginTopLevelMenu(withId(tr("Session"), "menu/session"))) {
    const auto active = sessions_.active();
    const bool stop = active && sessions_.startStopAction(*active) == gui_session::StartStop::Stop;
    const bool enabled = active && (stop ? sessions_.canStop(*active) : sessions_.canStart(*active));
    if (ImGui::MenuItem(stop ? tr("Stop") : tr("Start"), nullptr, false, enabled)) {
      if (stop) {
        pressStop(*active);
      } else {
        pressStart(*active);
      }
    }
    {
      const auto activeUi = active ? sessionUi_.find(*active) : sessionUi_.end();
      const bool lanes = activeUi != sessionUi_.end() && hasLanes(activeUi->second);
      if (ImGui::MenuItem(tr("Reset lane order"), nullptr, false, lanes)) {
        resetLaneOrder(activeUi->second);
      }
    }
    ImGui::Separator();
    bool ask = prefs_.unsavedCapture == gui_prefs::UnsavedCapturePolicy::Ask;
    if (ImGui::MenuItem(tr("Ask before replacing or closing unsaved capture"), nullptr, &ask)) {
      prefs_.unsavedCapture = ask ? gui_prefs::UnsavedCapturePolicy::Ask : gui_prefs::UnsavedCapturePolicy::DontSave;
      savePreferencesNow();
    }
    ImGui::EndMenu();
  }
  if (beginTopLevelMenu(withId(tr("Help"), "menu/help"))) {
    if (ImGui::MenuItem(tr("Keyboard shortcuts"))) {
      showKeyboardShortcuts_ = true;
      gui_popup::open("keyboard_shortcuts");
    }
    ImGui::Separator();
    if (ImGui::MenuItem(tr("About bitoscope"))) {
      showAbout_ = true;
      gui_popup::open("about");
    }
    ImGui::EndMenu();
  }
  ImGui::EndMainMenuBar();
}

void GuiApp::drawStatusBar() {
  const ImGuiWindowFlags flags =
    ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_MenuBar;
  if (ImGui::BeginViewportSideBar("##statusbar", ImGui::GetMainViewport(), ImGuiDir_Down,
                                  ImGui::GetFrameHeight(), flags)) {
    if (ImGui::BeginMenuBar()) {
      const auto active = sessions_.active();
      const gui_session::Session* session = active ? sessions_.find(*active) : nullptr;
      if (session == nullptr) {
        ImGui::TextUnformatted(tr("No session"));
      } else {
        const std::string capture = captureStatusText(session->id);
        ImGui::TextUnformatted((capture.empty() ? trf("{}: idle", session->name)
                                                : trf("{}: {}", session->name, capture))
                                 .c_str());
      }
      const std::string right =
        zoomPercent_ == gui_prefs::kZoomDefaultPercent
          ? trf("Prototype - {} - {}", translator_.language(), darkInUse() ? tr("dark") : tr("light"))
          : trf("Prototype - {} - {} - {}%", translator_.language(),
                darkInUse() ? tr("dark") : tr("light"), zoomPercent_);
      const float width = ImGui::CalcTextSize(right.c_str()).x;
      ImGui::SameLine(ImGui::GetWindowWidth() - width - ImGui::GetStyle().WindowPadding.x * 2);
      ImGui::TextDisabled("%s", right.c_str());
      ImGui::EndMenuBar();
    }
  }
  ImGui::End();
}

void GuiApp::buildDefaultLayout(ImGuiID dockspace) {
  ImGuiViewport* viewport = ImGui::GetMainViewport();
  ImGui::DockBuilderRemoveNode(dockspace);
  ImGui::DockBuilderAddNode(dockspace, ImGuiDockNodeFlags_DockSpace);
  ImGui::DockBuilderSetNodeSize(dockspace, viewport->WorkSize);
  for (const auto& session : sessions_.sessions()) {
    ImGui::DockBuilderDockWindow(sessionWindowTitle(session).c_str(), dockspace);
  }
  ImGui::DockBuilderFinish(dockspace);
}

void GuiApp::dockLogAtBottom(ImGuiID dockspace) {
  const ImGuiDockNode* root = ImGui::DockBuilderGetNode(dockspace);
  if (root == nullptr || root->IsSplitNode()) {
    return;
  }
  ImGuiID rest = 0;
  const ImGuiID bottom =
    ImGui::DockBuilderSplitNode(dockspace, ImGuiDir_Down, 0.28f, nullptr, &rest);
  ImGui::DockBuilderDockWindow(withId("", kLogId).c_str(), bottom);
  ImGui::DockBuilderFinish(dockspace);
}

void GuiApp::drawNoSessionHint() {
  const std::string hint = trf("No session yet. {} to start one.", tr("File > New session..."));
  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  const ImVec2 center(viewport->WorkPos.x + viewport->WorkSize.x * 0.5f,
                      viewport->WorkPos.y + viewport->WorkSize.y * 0.5f);
  const ImVec2 textSize = ImGui::CalcTextSize(hint.c_str());
  const ImGuiDockNode* root = ImGui::DockBuilderGetNode(ImGui::GetID(kDockSpace));
  ImDrawList* draw = root != nullptr && root->HostWindow != nullptr ? root->HostWindow->DrawList
                                                                    : ImGui::GetBackgroundDrawList();
  draw->AddText(ImVec2(center.x - textSize.x * 0.5f, center.y - textSize.y * 0.5f),
               ImGui::GetColorU32(ImGuiCol_TextDisabled), hint.c_str());
}

void GuiApp::handleZoomShortcuts() {
  const ImGuiIO& io = ImGui::GetIO();
  if (io.WantTextInput) {
    return;
  }
  if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Equal, false)) {
    zoomIn();
  }
  if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Minus, false)) {
    zoomOut();
  }
  if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Keypad0, false)) {
    resetZoom();
  }
}

void GuiApp::drawFrame() {
  if (ImGui::IsKeyPressed(ImGuiKey_F11, false)) {
    toggleFullScreen();
  }

  handleZoomShortcuts();
  drawMenuBar();
  drawStatusBar();

  const ImGuiID dockspace = ImGui::GetID(kDockSpace);
  if (resetLayout_ || (!layoutBuilt_ && ImGui::DockBuilderGetNode(dockspace) == nullptr)) {
    buildDefaultLayout(dockspace);
    resetLayout_ = false;
  }
  layoutBuilt_ = true;
  if (dockLogAtBottom_) {
    dockLogAtBottom(dockspace);
    dockLogAtBottom_ = false;
  }
  ImGui::DockSpaceOverViewport(dockspace, ImGui::GetMainViewport());
  const ImGuiDockNode* central = ImGui::DockBuilderGetCentralNode(dockspace);
  const ImGuiID centralId = central != nullptr ? central->ID : 0;

  const std::vector<gui_session::Session> sessions = sessions_.sessions();
  if (sessions.empty()) {
    drawNoSessionHint();
  }
  for (const auto& session : sessions) {
    syncCaptureSettings(session, sessionUi_[session.id]);
    drawSession(session, centralId);
  }
  if (toolPanels_.logOpen) {
    drawLog();
  }
  if (showAbout_) {
    drawAbout();
  }
  if (showKeyboardShortcuts_) {
    drawKeyboardShortcuts();
  }
  drawNewSessionWizard();
  drawCloseModal();
  drawReplaceModal();
  drawFileErrorModal();
}

void GuiApp::drawSession(const gui_session::Session& session, ImGuiID centralNode) {
  if (centralNode != 0) {
    ImGui::SetNextWindowDockID(centralNode, ImGuiCond_FirstUseEver);
  }
  bool open = true;
  bool* closeButton = sessions_.canClose(session.id) ? &open : nullptr;
  const std::string title = sessionWindowTitle(session);
  if (focusSession_ == session.id && focusDelayFrames_ == 0) {
    ImGui::SetNextWindowFocus();
    if (const ImGuiWindow* window = ImGui::FindWindowByName(title.c_str());
        window != nullptr && window->DockNode != nullptr && window->DockNode->TabBar != nullptr) {
      window->DockNode->TabBar->NextSelectedTabId = window->TabId;
    }
    focusSession_.reset();
  }
  if (ImGui::Begin(title.c_str(), closeButton)) {
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
      sessions_.setActive(session.id);
    }
    ImGui::PushID(static_cast<int>(session.id));
    SessionUi& ui = sessionUi_[session.id];
    drawSessionToolbar(session, ui);
    ImGui::Separator();
    drawSessionBody(session, ui);
    drawStartModals(session, ui);
    ImGui::PopID();
  }
  ImGui::End();
  if (!open) {
    requestPanelClose(session.id);
  }
}

void GuiApp::closeSession(gui_session::SessionId id) {
  const gui_session::Session* session = sessions_.find(id);
  if (session == nullptr) {
    return;
  }
  const std::string name = session->name;
  if (!sessions_.close(id)) {
    return;
  }
  SessionUi& closing = sessionUi_[id];
  bool sharedElsewhere = false;
  if (!closing.capabilitiesKey.empty()) {
    for (const auto& [otherId, otherUi] : sessionUi_) {
      if (otherId != id && otherUi.capabilitiesKey == closing.capabilitiesKey) {
        sharedElsewhere = true;
        break;
      }
    }
  }
  if (!sharedElsewhere) {
    constexpr bool switchOff = true;
    syncGeneratorOutputs(closing, switchOff);
  }
  retireWaveform(closing);
  sessionUi_.erase(id);
  logger_.logf(log::LogLevel::info, "gui", "Session closed: {}", name);
  savePreferencesNow();
}

void GuiApp::drawSessionToolbar(const gui_session::Session& session, SessionUi& ui) {
  const bool stop = sessions_.startStopAction(session.id) == gui_session::StartStop::Stop;
  const bool enabled = stop ? sessions_.canStop(session.id) && !ui.capture.stopRequested()
                            : sessions_.canStart(session.id);
  ImGui::BeginDisabled(!enabled);
  if (iconTextButton("startstop", stop ? Icon::Stop : Icon::Start, stop ? tr("Stop") : tr("Start"))) {
    sessions_.setActive(session.id);
    if (stop) {
      pressStop(session.id);
    } else {
      pressStart(session.id);
    }
  }
  ImGui::EndDisabled();
  const auto why = stop ? sessions_.stopDisabledReason(session.id)
                        : sessions_.startDisabledReason(session.id);
  if (why) {
    tooltip(disabledReasonText(*why).c_str());
  }
  ImGui::SameLine();
  const auto saveBlocked = ui.capture.saveBlocked();
  ImGui::BeginDisabled(saveBlocked.has_value());
  if (iconButton("save", Icon::Save,
                 saveBlocked ? saveBlockedText(*saveBlocked).c_str() : tr("Save capture..."))) {
    sessions_.setActive(session.id);
    requestSave(session.id, false, gui_session_file::SaveCommand::Save);
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  if (iconButton("load", Icon::Open, tr("Open saved session..."))) {
    requestOpen();
  }

  ImGui::SameLine(0, ImGui::GetStyle().ItemSpacing.x * 3);
  ImGui::AlignTextToFramePadding();
  if (!session.openedFrom.empty()) {
    ImGui::TextUnformatted(ui.device.c_str());
    tooltip(ui.openedInfo.c_str());
  } else if (session.device == std::nullopt) {
    ImGui::TextDisabled("%s", tr("No device"));
    tooltip(tr("Device"));
  } else {
    const std::string driverLine =
      driverVersionLine(gui_devices::shownDriverName(router_.catalog(), session.device->driver),
                        gui_devices::sessionDriverVersion(router_, *session.device));
    if (session.deviceState == gui_session::DeviceState::Connected) {
      ImGui::TextUnformatted(ui.device.c_str());
      tooltip((std::string(tr("Device")) + "\n" + driverLine).c_str());
    } else {
      const gui_devices::HeaderStatus status = gui_devices::sessionHeaderStatus(router_, session);
      ImGui::TextDisabled("%s", ui.device.c_str());
      tooltip(driverLine.c_str());
      ImGui::SameLine();
      if (status.kind == gui_devices::HeaderKind::Lost) {
        ImGui::PushStyleColor(ImGuiCol_Text, errorTextColor(darkInUse()));
        ImGui::TextUnformatted(tr("(disconnected)"));
        ImGui::PopStyleColor();
        tooltip((trf("Device lost: {}", status.detail) + "\n" + driverLine).c_str());
      } else if (status.kind == gui_devices::HeaderKind::Waiting) {
        ImGui::TextDisabled("%s", tr("(waiting)"));
        tooltip((trf("Waiting for device: {}", reasonWithDetail(status.reason, status.detail)) + "\n" + driverLine)
                  .c_str());
      } else {
        const std::string progress = reasonWithDetail(status.reason, status.detail);
        ImGui::TextDisabled("%s", progress.empty() ? tr("(connecting...)")
                                                   : trf("(connecting: {})", progress).c_str());
        tooltip((std::string(tr("Connecting to device...")) + "\n" + driverLine).c_str());
      }
    }
  }

  if (ui.settings && ui.capabilities) {
    ImGui::BeginDisabled(session.running);
    ImGui::SameLine(0, ImGui::GetStyle().ItemSpacing.x * 3);
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 7.0f);
    rateCombo(ui, withId("", "rate").c_str());
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 13.0f);
    lengthCombo(ui, withId("", "length").c_str());
    ImGui::EndDisabled();
  }
  drawWaveformToolbar(ui);

  drawCaptureStatus(ui);

}

void GuiApp::drawSessionBody(const gui_session::Session& session, SessionUi& ui) {
  const bool onLeft = propertiesSide_ == gui_prefs::PropertiesSide::Left;
  const bool open = ui.propertiesPanel.isShown();
  const float scale = uiScale();
  const float available = ImGui::GetContentRegionAvail().x;
  const float height = ImGui::GetContentRegionAvail().y;
  const float left = ImGui::GetCursorScreenPos().x;
  const float right = left + available;
  const float splitter = kSplitterWidth * scale;
  const float activityBar =
    static_cast<float>(iconPixelSize()) + ImGui::GetStyle().FramePadding.x * 2 +
    ImGui::GetStyle().WindowPadding.x * 2;
  const float narrowest = kPropertiesWidthMin * scale;
  static_assert(kChannelHeaderEm * 100.0f == static_cast<float>(gui_prefs::kLaneNameWidthDefaultCentiEm));
  const float fontSize = ImGui::GetFontSize();
  const float laneNarrowest = gui_prefs::laneNameWidthPixels(gui_prefs::kLaneNameWidthMinCentiEm, fontSize, 0.0f);
  const float widest = (std::max)(
    narrowest, available - laneNarrowest - splitter - activityBar - kSignalsWidthMin * scale);
  float propertiesWidth = 0;
  if (open) {
    const float wanted = ui.propertiesWidth >= 1.0f ? ui.propertiesWidth : kPropertiesWidthDefault;
    propertiesWidth = std::clamp(wanted * scale, narrowest, widest);
  }
  waveform_view::LaneNameFitInput nameFit;
  nameFit.available = available;
  nameFit.activityBar = activityBar;
  nameFit.properties = open ? splitter + propertiesWidth : 0.0f;
  nameFit.wanted =
    gui_prefs::laneNameWidthPixels(laneNameWidthCentiEm_, fontSize, (std::numeric_limits<float>::max)());
  nameFit.laneMinimum = laneNarrowest;
  nameFit.tracesMinimum = kSignalsWidthMin * scale;
  const waveform_view::LaneNameFit laneFit = waveform_view::fitLaneNameWidth(nameFit);
  const float laneWidth = std::floor(static_cast<float>(laneFit.laneNameWidth));
  const float signalsWidth = (std::max)(
    1.0f, available - laneWidth - activityBar - (open ? splitter + propertiesWidth : 0));

  const auto drawLanesAndSignals = [&] {
    std::optional<gui_capture::LiveCaptureSlot::View> live;
    if (ui.slot) {
      live.emplace(ui.slot->view());
    }
    {
      BITOSCOPE_DRAW_PART(syncPart, frameProbe_, Prologue);
      syncWaveform(session.id, ui, live ? &*live : nullptr);
    }
    drawWaveform(session.id, ui, laneWidth, laneWidth + signalsWidth, kSignalsWidthMin * scale);
  };

  const auto drawSplitter = [&] {
    ImGui::InvisibleButton("splitter", ImVec2(splitter, (std::max)(1.0f, height)));
    const bool dragging = ImGui::IsItemActive();
    const bool hovered = ImGui::IsItemHovered();
    if (ImGui::IsItemActivated()) {
      ui.splitterGrabOffset = ImGui::GetIO().MousePos.x - (ImGui::GetItemRectMin().x + ImGui::GetItemRectMax().x) * 0.5f;
    }
    if (hovered || dragging) {
      ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    }
    if (dragging && ImGui::IsMousePosValid()) {
      const gui_session::SplitterDrag geometry{onLeft,   left,   right,  activityBar,
                                               splitter, narrowest, widest, ui.splitterGrabOffset};
      ui.propertiesWidth =
        gui_session::propertiesWidthAtPointer(geometry, ImGui::GetIO().MousePos.x) / scale;
    }
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    const float x = std::floor((min.x + max.x) * 0.5f);
    const ImGuiCol color = dragging  ? ImGuiCol_SeparatorActive
                           : hovered ? ImGuiCol_SeparatorHovered
                                     : ImGuiCol_Separator;
    ImGui::GetWindowDrawList()->AddLine(ImVec2(x + 0.5f, min.y + 0.5f), ImVec2(x + 0.5f, max.y + 0.5f), ImGui::GetColorU32(color),
                                        (std::max)(1.0f, scale));
  };

  const auto drawProperties = [&] {
    if (ImGui::BeginChild("properties", ImVec2(propertiesWidth, 0), ImGuiChildFlags_Borders)) {
      switch (ui.propertiesPanel.selectedView()) {
      case gui_session::PropertiesView::Device:
        drawDeviceProperties(ui);
        break;
      case gui_session::PropertiesView::Decoders:
        drawDecodersProperties(ui);
        break;
      }
    }
    ImGui::EndChild();
  };
  const auto drawBar = [&] { drawPropertiesActivityBar(ui, activityBar); };

  const std::vector<gui_session::PropertiesColumn> order =
    gui_session::propertiesLayout(onLeft, open);
  for (std::size_t i = 0; i < order.size(); ++i) {
    if (i > 0) {
      ImGui::SameLine(0, 0);
    }
    switch (order[i]) {
    case gui_session::PropertiesColumn::Bar:
      drawBar();
      break;
    case gui_session::PropertiesColumn::Pane:
      drawProperties();
      break;
    case gui_session::PropertiesColumn::Splitter:
      drawSplitter();
      break;
    case gui_session::PropertiesColumn::Waveform:
      drawLanesAndSignals();
      break;
    }
  }
}

void GuiApp::drawPropertiesActivityBar(SessionUi& ui, float width) {
  if (ImGui::BeginChild("propertiesActivityBar", ImVec2(width, 0))) {
    const auto button = [&](const char* id, Icon icon, gui_session::PropertiesView view,
                            const char* label) {
      const std::optional<gui_session::PropertiesView> shown = ui.propertiesPanel.shownView();
      const bool selected = shown.has_value() && *shown == view;
      const float buttonWidth = static_cast<float>(iconPixelSize()) + ImGui::GetStyle().FramePadding.x * 2;
      ImGui::SetCursorPosX(ImGui::GetCursorPosX() + gui_session::centredOffset(width, buttonWidth));
      constexpr bool mirrored = false;
      if (iconButton(id, icon, label, mirrored, selected)) {
        ui.propertiesPanel.click(view);
      }
    };
    button("propertiesViewDevice", Icon::Device, gui_session::PropertiesView::Device, tr("Device"));
    button("propertiesViewDecoders", Icon::Decoders, gui_session::PropertiesView::Decoders,
           tr("Decoders"));
  }
  ImGui::EndChild();
}

void GuiApp::drawDeviceProperties(SessionUi& ui) {
  ImGui::SeparatorText(tr("Capture"));
  const auto connectedChannels = [&] {
    std::vector<std::uint32_t> connected;
    for (const auto& decoder : ui.decoders) {
      const std::vector<std::uint32_t> bound = decoder.boundDeviceChannels();
      connected.insert(connected.end(), bound.begin(), bound.end());
    }
    return connected;
  };
  if (!ui.openedFrom.empty()) {
    const gui_format::PropertyTreeColumns columns = propertyColumnsFor({tr("Channels")});
    propertyRowLabel(tr("Channels"), columns,
                     tr("Channels of file; lane is hidden while its channel is not selected"));
    drawChannelSelector(ui.fileLanesShown, ui.channelColors, connectedChannels(), ui.keptDeviceChannels);
  } else if (!ui.settings || !ui.capabilities) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", ui.capabilitiesProblem.empty()
                               ? tr("Capture settings of this device are not known yet.")
                               : trf("This device cannot capture: {}", ui.capabilitiesProblem).c_str());
    ImGui::PopStyleColor();
  } else {
    ImGui::BeginDisabled(ui.capture.running());
    const gui_format::PropertyTreeColumns columns =
      propertyColumnsFor({tr("Sample rate"), tr("Length"), tr("Channels"), tr("Pre-trigger")});
    propertyRowLabel(tr("Sample rate"), columns, tr("Fixed while device runs"));
    rateCombo(ui, "##capture_rate");
    propertyRowLabel(tr("Length"), columns);
    lengthCombo(ui, "##capture_length");
    propertyRowLabel(tr("Channels"), columns,
                     tr("Channels next capture takes; lane of capture shown is hidden "
                        "while its channel is not selected"));
    drawChannelSelector(ui.settings->channels, ui.channelColors, connectedChannels());
    propertyRowLabel(tr("Pre-trigger"), columns,
                     tr("Share of length before trigger, when channel has one"));
    int percent = static_cast<int>(ui.settings->preTriggerPercent);
    if (ImGui::SliderInt("##pretrigger", &percent, 0, 100, "%d %%")) {
      ui.settings->preTriggerPercent = static_cast<std::uint32_t>(std::clamp(percent, 0, 100));
    }
    ImGui::EndDisabled();
  }

  if (!ui.deviceProperties) {
    ImGui::SeparatorText(tr("Properties"));
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", tr("Properties of this device are not known yet"));
    ImGui::PopStyleColor();
    return;
  }
  const bool capturing = ui.capture.running();
  std::vector<std::string> propertyFacets;
  std::vector<std::string> generatorFacets;
  for (const std::string& facetName : ui.deviceProperties->facetNames()) {
    (gui_properties::isGeneratorFacet(facetName) ? generatorFacets : propertyFacets).push_back(facetName);
  }
  drawPropertySection(ui, tr("Properties"), propertyFacets, capturing);

  if (!generatorFacets.empty()) {
    drawPropertySection(ui, tr("Generator"), generatorFacets, capturing);
    drawGeneratorSwitchOffProblems(ui);
  }
}

void GuiApp::drawGeneratorSwitchOffProblems(SessionUi& ui) {
  for (const gui_properties::GeneratorSwitchOff& generator : ui.generatorSwitchOff) {
    const gui_properties::FacetPropertiesModel* model = ui.deviceProperties->facet(generator.facet);
    if (model == nullptr || !model->anyStateUnknown()) {
      continue;
    }
    ImGui::PushStyleColor(ImGuiCol_Text, errorTextColor(darkInUse()));
    if (!generator.unreadable.empty()) {
      ImGui::TextWrapped("%s", trf("Outputs could not be switched off, so their state is unknown: {}",
                                   generator.unreadable).c_str());
    }
    for (const gui_properties::OutputLeftUnknown& output : generator.leftUnknown) {
      ImGui::TextWrapped("%s", trf("Output {} could not be switched off, so its state is unknown: {}", output.output,
                                   output.reason).c_str());
    }
    ImGui::PopStyleColor();
  }
}

void GuiApp::drawPropertySection(SessionUi& ui, const char* title, const std::vector<std::string>& facets,
                                 bool capturing) {
  const bool withNodes = gui_properties::facetNodesShown(facets.size());
  gui_properties::FacetPropertiesModel* lone =
    !withNodes && !facets.empty() ? ui.deviceProperties->facet(facets.front()) : nullptr;
  const float headingRight = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
  ImGui::SeparatorText(title);
  if (lone != nullptr && lone->anyChangedFromDefault()) {
    const char* label = tr("Reset all");
    ImGui::SameLine(headingRight - ImGui::CalcTextSize(label).x - ImGui::GetStyle().FramePadding.x * 2.0f);
    ImGui::PushID(facets.front().c_str());
    if (ImGui::SmallButton(label)) {
      resetFacetToDefault(ui, facets.front(), *lone, capturing);
    }
    ImGui::PopID();
  }
  for (const std::string& facetName : facets) {
    gui_properties::FacetPropertiesModel* model = ui.deviceProperties->facet(facetName);
    if (model == nullptr) {
      continue;
    }
    drawPropertyFacetSection(ui, facetName, *model, capturing, withNodes);
  }
}

void GuiApp::resetFacetToDefault(SessionUi& ui, const std::string& facetName,
                                 gui_properties::FacetPropertiesModel& model, bool capturing) {
  const auto generator =
    std::find_if(ui.generatorSwitchOff.begin(), ui.generatorSwitchOff.end(),
                 [&](const gui_properties::GeneratorSwitchOff& g) { return g.facet == facetName; });
  if (generator != ui.generatorSwitchOff.end() && pipeHost_ != nullptr) {
    auto reset = gui_properties::resetGeneratorFromPanel(*generator, model, capturing, pipeHost_->pluginHost(),
                                                         ui.capabilitiesKey);
    if (!reset) {
      status(trf("Could not change '{}': {}", facetName, reset.error()));
      return;
    }
  } else {
    model.resetAllToDefault();
  }
  ui.propertyTextBuffers.clear();
  if (gui_properties::isGeneratorFacet(facetName)) {
    sendFacetPropertiesNow(ui, facetName, model.valuesToSend());
  }
}

void GuiApp::drawPropertyFacetSection(SessionUi& ui, const std::string& facetName,
                                      gui_properties::FacetPropertiesModel& model, bool capturing, bool withNode) {
  ImGui::PushID(facetName.c_str());
  if (model.displayTree().empty()) {
    if (withNode) {
      ImGui::AlignTextToFramePadding();
      ImGui::TextUnformatted(facetName.c_str());
      ImGui::SameLine();
    }
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextUnformatted(tr("(no properties)"));
    ImGui::PopStyleColor();
  } else if (!withNode) {
    drawPropertyFacet(ui, facetName, model, capturing);
  } else {
    ImGui::SetNextItemOpen(true, ImGuiCond_FirstUseEver);
    if (ImGui::TreeNodeEx(facetName.c_str(), ImGuiTreeNodeFlags_SpanAvailWidth)) {
      if (model.anyChangedFromDefault()) {
        ImGui::SameLine();
        if (ImGui::SmallButton(tr("Reset all"))) {
          resetFacetToDefault(ui, facetName, model, capturing);
        }
      }
      drawPropertyFacet(ui, facetName, model, capturing);
      ImGui::TreePop();
    }
  }
  ImGui::PopID();
}

void GuiApp::drawPropertyFacet(SessionUi& ui, const std::string& facetName, gui_properties::FacetPropertiesModel& model,
                               bool capturing) {
  drawPropertyChildren(ui, facetName, model, model.displayTree(), capturing);
}

void GuiApp::drawPropertyChildren(SessionUi& ui, const std::string& facetName, gui_properties::FacetPropertiesModel& model,
                                  const std::vector<gui_properties::PropertyTreeNode>& children, bool capturing) {
  std::vector<std::string> labels;
  for (const gui_properties::PropertyTreeNode& child : children) {
    if (const auto* property = std::get_if<gui_properties::PropertyDisplay>(&child.node)) {
      labels.push_back(property->label);
    }
  }
  const gui_format::PropertyTreeColumns columns = propertyColumnsFor(labels);
  for (const gui_properties::PropertyTreeNode& child : children) {
    if (const auto* group = std::get_if<gui_properties::PropertyGroupDisplay>(&child.node)) {
      ImGui::PushID(group->name.c_str());
      ImGui::SetNextItemOpen(true, ImGuiCond_FirstUseEver);
      if (ImGui::TreeNodeEx(group->name.c_str(), ImGuiTreeNodeFlags_SpanAvailWidth)) {
        drawPropertyChildren(ui, facetName, model, group->children, capturing);
        ImGui::TreePop();
      }
      ImGui::PopID();
      continue;
    }
    const auto& property = std::get<gui_properties::PropertyDisplay>(child.node);
    ImGui::PushID(property.id.empty() ? "(unknown)" : property.id.c_str());
    drawPropertyRow(ui, facetName, model, property, capturing, columns);
    ImGui::PopID();
  }
}

void GuiApp::drawPropertyRow(SessionUi& ui, const std::string& facetName, gui_properties::FacetPropertiesModel& model,
                             const gui_properties::PropertyDisplay& property, bool capturing,
                             gui_format::PropertyTreeColumns columns) {
  using gui_properties::PropertyKind;
  const bool lockedByCapture = capturing && !property.changeableDuringCapture && !property.id.empty();
  const bool sendAtOnce = gui_properties::isGeneratorFacet(facetName) || (capturing && property.changeableDuringCapture);
  std::string help = property.help;
  if (lockedByCapture) {
    if (!help.empty()) {
      help += "\n";
    }
    help += tr("Fixed while device captures");
  }
  const bool stateUnknown = model.isStateUnknown(property.id);
  if (stateUnknown) {
    if (!help.empty()) {
      help += "\n";
    }
    help += tr("State unknown: switching off failed when device was opened. Click switches it off.");
  }
  propertyRowLabel(property.label.c_str(), columns, help.empty() ? nullptr : help.c_str());

  if (property.id.empty()) {
    ImGui::BeginDisabled();
    ImGui::TextUnformatted("-");
    ImGui::EndDisabled();
    return;
  }

  const std::string bufferKey = facetName + "/" + property.id;
  ImGui::BeginDisabled(lockedByCapture);
  switch (property.kind) {
  case PropertyKind::Integer:
  case PropertyKind::Decimal: {
    auto [it, inserted] = ui.propertyTextBuffers.try_emplace(bufferKey);
    std::array<char, 96>& buffer = it->second;
    if (inserted) {
      const std::string text = model.textOf(property.id);
      const std::size_t n = (std::min)(text.size(), buffer.size() - 1);
      std::copy_n(text.begin(), n, buffer.begin());
      buffer[n] = '\0';
    }
    if (gui_text_field::committedInputText("##val", buffer.data(), buffer.size()) != gui_text_field::Commit::None) {
      auto set = model.setText(property.id, buffer.data(), capturing);
      if (set.has_value()) {
        const std::string text = model.textOf(property.id);
        const std::size_t n = (std::min)(text.size(), buffer.size() - 1);
        std::copy_n(text.begin(), n, buffer.begin());
        buffer[n] = '\0';
        if (sendAtOnce) {
          if (const gui_properties::PropertyValue* value = model.valueOf(property.id)) {
            sendPropertyNow(ui, facetName, *value);
          }
        }
      }
    }
    break;
  }
  case PropertyKind::Boolean: {
    const gui_properties::PropertyValue* value = model.valueOf(property.id);
    // WHY: Unknown counts as on, so a click on it switches off: only stop command makes state known again.
    bool checked = stateUnknown ||
                   ((value != nullptr) && std::holds_alternative<bool>(value->value) && std::get<bool>(value->value));
    ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, stateUnknown);
    const bool clicked = ImGui::Checkbox("##val", &checked);
    ImGui::PopItemFlag();
    if (clicked) {
      const auto generator =
        std::find_if(ui.generatorSwitchOff.begin(), ui.generatorSwitchOff.end(),
                     [&](const gui_properties::GeneratorSwitchOff& g) { return g.facet == facetName; });
      std::expected<void, std::string> set;
      if (!checked && generator != ui.generatorSwitchOff.end() && pipeHost_ != nullptr) {
        set = gui_properties::switchOutputOffFromPanel(*generator, model, property.id, capturing,
                                                       pipeHost_->pluginHost(), ui.capabilitiesKey);
        if (!set) {
          status(trf("Could not change '{}': {}", property.label, set.error()));
        }
      } else {
        set = model.setBoolean(property.id, checked, capturing);
      }
      if (set.has_value() && sendAtOnce) {
        if (const gui_properties::PropertyValue* sent = model.valueOf(property.id)) {
          sendPropertyNow(ui, facetName, *sent);
        }
      }
    }
    break;
  }
  case PropertyKind::Enumeration: {
    std::vector<std::string> choiceTexts;
    choiceTexts.reserve(property.choices.size());
    for (const gui_properties::EnumerationChoice& choice : property.choices) {
      choiceTexts.push_back(choice.text);
    }
    int index = 0;
    if (const gui_properties::PropertyValue* value = model.valueOf(property.id)) {
      if (const auto* enumIndex = std::get_if<bitoscope::ipc::props::EnumIndex>(&value->value)) {
        index = static_cast<int>(enumIndex->index);
      }
    }
    if (!choiceTexts.empty() && comboOf("##val", index, choiceTexts)) {
      auto set = model.setChoice(property.id, static_cast<std::uint32_t>(index), capturing);
      if (set.has_value() && sendAtOnce) {
        if (const gui_properties::PropertyValue* sent = model.valueOf(property.id)) {
          sendPropertyNow(ui, facetName, *sent);
        }
      }
    }
    break;
  }
  case PropertyKind::Unknown:
    ImGui::TextUnformatted(tr("(unsupported type)"));
    break;
  }
  ImGui::EndDisabled();

  if (auto error = model.errorFor(property.id)) {
    ImGui::PushStyleColor(ImGuiCol_Text, errorTextColor(darkInUse()));
    ImGui::TextWrapped("%s", error->c_str());
    ImGui::PopStyleColor();
  }
  if (property.kind != PropertyKind::Unknown && model.isChangedFromDefault(property.id)) {
    ImGui::SameLine();
    if (ImGui::SmallButton(tr("Reset"))) {
      const auto generator =
        std::find_if(ui.generatorSwitchOff.begin(), ui.generatorSwitchOff.end(),
                     [&](const gui_properties::GeneratorSwitchOff& g) { return g.facet == facetName; });
      std::expected<void, std::string> reset;
      if (generator != ui.generatorSwitchOff.end() && pipeHost_ != nullptr) {
        reset = gui_properties::resetOutputFromPanel(*generator, model, property.id, capturing, pipeHost_->pluginHost(),
                                                     ui.capabilitiesKey);
      } else {
        model.resetToDefault(property.id);
      }
      if (!reset) {
        status(trf("Could not change '{}': {}", property.label, reset.error()));
        return;
      }
      ui.propertyTextBuffers.erase(bufferKey);
      if (sendAtOnce) {
        if (const gui_properties::PropertyValue* value = model.valueOf(property.id)) {
          sendPropertyNow(ui, facetName, *value);
        }
      }
    }
  }
}

void GuiApp::drawChannelSelector(std::vector<bool>& selected, const std::vector<waveform_view::ColorChoice>& colors,
                                 const std::vector<std::uint32_t>& connected,
                                 const std::vector<std::uint32_t>& entryChannel) {
  namespace wv = waveform_view;
  namespace gc = gui_capture;
  const bool dark = darkInUse();
  const float em = ImGui::GetFontSize();
  const float buttonSize = std::round(em * 1.7f);
  const float rounding = std::round(buttonSize * 0.3f);
  const float outline = (std::max)(1.0f, std::round(uiScale()));
  const float gap = ImGui::GetStyle().ItemSpacing.x;
  ImDrawList* draw = ImGui::GetWindowDrawList();
  const float controlX = ImGui::GetCursorScreenPos().x;
  const std::size_t perRow = static_cast<std::size_t>(
    gui_format::buttonsPerRow(ImGui::GetContentRegionAvail().x, buttonSize, gap));

  for (std::size_t i = 0; i < selected.size(); ++i) {
    if (i % perRow != 0) {
      ImGui::SameLine(0.0f, gap);
    } else if (i != 0) {
      ImGui::SetCursorScreenPos(ImVec2(controlX, ImGui::GetCursorScreenPos().y));
    }
    const std::uint32_t channel = i < entryChannel.size() ? entryChannel[i] : static_cast<std::uint32_t>(i);
    const wv::SelectorButtonColors button = wv::selectorButtonColors(gc::channelColorOrPalette(colors, channel), dark);
    // WHY: Raw ImU32 colours skip the style alpha that BeginDisabled lowers, so apply it here.
    const ImU32 color = ImGui::GetColorU32(IM_COL32(button.fill.r, button.fill.g, button.fill.b, 255));
    const bool needsOutline = button.outlined;
    const ImU32 outlineColor =
      ImGui::GetColorU32(IM_COL32(button.outline.r, button.outline.g, button.outline.b, 255));
    const ImU32 markColor =
      ImGui::GetColorU32(IM_COL32(button.unselectedMark.r, button.unselectedMark.g, button.unselectedMark.b, 255));
    const ImU32 selectedTextColor =
      ImGui::GetColorU32(IM_COL32(button.selectedText.r, button.selectedText.g, button.selectedText.b, 255));
    const bool isSelected = selected[i];
    const bool isOnlySelected =
      isSelected && std::count(selected.begin(), selected.end(), true) == 1;

    ImGui::PushID(static_cast<int>(i));
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##channel_toggle", ImVec2(buttonSize, buttonSize));
    const bool hovered = ImGui::IsItemHovered();
    const bool clicked = ImGui::IsItemClicked();
    const ImVec2 max(pos.x + buttonSize, pos.y + buttonSize);
    if (isSelected) {
      draw->AddRectFilled(pos, max, color, rounding);
      if (needsOutline) {
        addEdgeCentredOutline(draw, pos, max, outlineColor, rounding, outline);
      }
    } else {
      addEdgeCentredOutline(draw, pos, max, markColor, rounding, outline);
    }
    const std::string label = std::to_string(channel);
    const ImVec2 textSize = ImGui::CalcTextSize(label.c_str());
    const ImU32 textColor = isSelected ? selectedTextColor : markColor;
    draw->AddText(ImVec2(pos.x + (buttonSize - textSize.x) * 0.5f, pos.y + (buttonSize - textSize.y) * 0.5f),
                 textColor, label.c_str());
    if (hovered && isOnlySelected) {
      ImGui::SetTooltip("%s", tr("At least one channel must stay selected"));
    }
    if (clicked) {
      wv::toggleChannelSelection(selected, static_cast<std::uint32_t>(i));
    }
    ImGui::PopID();
  }
  ImGui::SetCursorScreenPos(ImVec2(controlX, ImGui::GetCursorScreenPos().y));
  if (ImGui::Button(withId(tr("All"), "channels/all").c_str())) {
    wv::selectAllChannels(selected);
  }
  ImGui::SameLine();
  if (ImGui::Button(withId(tr("One"), "channels/one").c_str())) {
    wv::selectOneChannel(selected);
  }
  ImGui::SameLine();
  const std::vector<std::uint32_t> connectedEntries =
    entryChannel.empty() ? connected : wv::lanesOfChannels(entryChannel, connected);
  const bool anyConnected = std::any_of(connectedEntries.begin(), connectedEntries.end(),
                                        [&](std::uint32_t c) { return c < selected.size(); });
  ImGui::BeginDisabled(!anyConnected);
  if (ImGui::Button(withId(tr("Connected only"), "channels/connected").c_str())) {
    wv::selectOnlyChannels(selected, connectedEntries);
  }
  ImGui::EndDisabled();
  if (!anyConnected && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
    ImGui::SetTooltip("%s", tr("No channel is connected to decoder"));
  }
}

void GuiApp::openNewSessionWizard() {
  for (const std::string& driver : router_.catalog().driversToRelist()) {
    router_.relisting(driver);
    if (deviceWorker_) {
      deviceWorker_->relist(driver);
    }
  }
  std::vector<std::string> ports = platform_.serialPorts();
  logger_.logf(log::LogLevel::info, "gui", "New-session wizard opened; {} driver(s) found",
               router_.catalog().drivers().size());
  wizard_.emplace(Wizard{
    gui_wizard::NewSessionWizard(router_.wizardDrivers(), std::move(ports)),
    {},
    {},
    false,
    router_.catalog().driversKnown()});
  wizardToOpen_ = true;
}

void GuiApp::createSession(const gui_wizard::NewDevice& device) {
  const auto& drivers = router_.catalog().drivers();
  if (device.search != gui_wizard::DeviceSearch::DeviceList || device.driver >= drivers.size()) {
    return;
  }
  const std::string driver = drivers[device.driver].name;
  const ipc::DeviceInfo* info = router_.catalog().find(driver, device.deviceId);
  if (info == nullptr) {
    status(trf("Device {} is gone: it was unplugged", device.deviceId));
    return;
  }
  createSessionFor(driver, *info);
}

void GuiApp::createSessionFor(const std::string& driver, const ipc::DeviceInfo& device) {
  const gui_session::SessionId id =
    sessions_.add([](std::uint64_t n) { return trf("Session {}", n); });
  const std::string label =
    gui_devices::deviceLabel(gui_devices::shownDriverName(router_.catalog(), driver), device);
  sessionUi_[id].device = label;
  logger_.logf(log::LogLevel::info, "gui", "New session: {}, device {}", sessions_.find(id)->name,
               label);
  applyRouterOutput(router_.bindSession(id, driver, device));
}

void GuiApp::drawNewSessionWizard() {
  const std::string title = withId(tr("New session"), kWizardId);
  if (wizardToOpen_) {
    ImGui::OpenPopup(title.c_str());
    wizardToOpen_ = false;
  }
  if (!wizard_) {
    return;
  }
  const float em = ImGui::GetFontSize();
  ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing,
                          ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(em * 36.0f, em * 24.0f), ImGuiCond_Appearing);
  const ImGuiWindowFlags flags =
    ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
  if (!ImGui::BeginPopupModal(title.c_str(), nullptr, flags)) {
    wizard_.reset();
    return;
  }
  Wizard& wizard = *wizard_;
  const gui_devices::DeviceCatalog& catalog = router_.catalog();
  if (!wizard.driversKnown && catalog.driversKnown()) {
    wizard.state = gui_wizard::NewSessionWizard(router_.wizardDrivers(), platform_.serialPorts());
    wizard.driversKnown = true;
  }
  gui_wizard::NewSessionWizard& state = wizard.state;
  const std::vector<gui_wizard::WizardDriver> now = router_.wizardDrivers();
  for (std::size_t i = 0; i < now.size(); ++i) {
    if (state.devices(i) != now[i].devices || state.unavailable(i) != now[i].unavailable) {
      state.setDevices(i, now[i].devices, now[i].unavailable);
    }
  }
  const auto& drivers = catalog.drivers();
  const bool onDriverPage = state.page() == gui_wizard::WizardPage::Driver;

  if (onDriverPage) {
    ImGui::TextUnformatted(tr("Step 1 of 2: Choose driver"));
  } else {
    const std::size_t driver = state.driver().value_or(0);
    ImGui::TextUnformatted(
      trf("Step 2 of 2: Choose device - {}", driver < drivers.size() ? drivers[driver].shownName() : "")
        .c_str());
  }
  ImGui::Separator();

  const float footer = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;
  bool advance = false;
  bool close = false;
  if (ImGui::BeginChild("page", ImVec2(0, -footer))) {
    if (onDriverPage) {
      if (!catalog.driversKnown()) {
        ImGui::TextDisabled("%s", tr("Looking for drivers..."));
      } else if (drivers.empty()) {
        ImGui::TextWrapped("%s", trf("No driver found in {}. Drivers are plugins in "
                                     "drivers folder beside bitoscope_gui.",
                                     catalog.directory().string())
                                   .c_str());
      }
      for (std::size_t i = 0; i < drivers.size() && i < now.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        if (ImGui::Selectable(drivers[i].shownName().c_str(), state.driver() == i,
                              ImGuiSelectableFlags_AllowDoubleClick)) {
          if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            state.activateListItem(i);
          } else {
            state.selectDriver(i);
          }
        }
        ImGui::Indent();
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped("%s", driverStatus(drivers[i]).c_str());
        ImGui::PopStyleColor();
        const gui_devices::DriverWatchMark watch = gui_devices::driverWatchMark(drivers[i]);
        if (watch.mark == gui_devices::WatchMark::NotWatched) {
          ImGui::PushStyleColor(ImGuiCol_Text, errorTextColor(darkInUse()));
          ImGui::TextWrapped("%s", tr("Not watched: devices plugged in or out go unnoticed"));
          ImGui::PopStyleColor();
          if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) {
            const std::string again = tr("Devices are looked for again each time this wizard opens");
            ImGui::SetTooltip("%s", watch.reason.empty()
                                      ? again.c_str()
                                      : (trf("Device watch stopped: {}", watch.reason) + "\n" + again).c_str());
          }
        }
        ImGui::Unindent();
        ImGui::Spacing();
        ImGui::PopID();
      }
    } else if (drawWizardDevicePage(wizard)) {
      close = true;
    }
  }
  ImGui::EndChild();
  ImGui::Separator();

  const ImGuiStyle& style = ImGui::GetStyle();
  const char* back = tr("< Back");
  const char* next = tr("Next >");
  const char* create = tr("Create");
  const char* cancel = tr("Cancel");
  float width = style.ItemSpacing.x * 3;
  for (const char* label : {back, next, create, cancel}) {
    width += ImGui::CalcTextSize(label).x + style.FramePadding.x * 2;
  }
  const float room = ImGui::GetContentRegionAvail().x;
  if (room > width) {
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + room - width);
  }
  ImGui::BeginDisabled(!state.canGoBack());
  if (ImGui::Button(back)) {
    state.back();
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::BeginDisabled(!state.canGoNext());
  if (ImGui::Button(next)) {
    advance = true;
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::BeginDisabled(!state.canCreate());
  if (ImGui::Button(create)) {
    if (const auto device = state.create()) {
      createSession(*device);
      close = true;
    }
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  const bool escape =
    ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !wizard.popupAbove &&
    !ImGui::IsAnyItemActive() &&
    ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows | ImGuiFocusedFlags_NoPopupHierarchy);
  if (ImGui::Button(cancel) || escape) {
    logger_.log(log::LogLevel::info, "gui", "New-session wizard cancelled");
    close = true;
  }
  if (advance) {
    state.next();
  }
  wizard.popupAbove = ImGui::GetCurrentContext()->OpenPopupStack.Size > 1;
  if (close) {
    ImGui::CloseCurrentPopup();
    wizard_.reset();
  }
  ImGui::EndPopup();
}

bool GuiApp::drawWizardDevicePage(Wizard& wizard) {
  gui_wizard::NewSessionWizard& state = wizard.state;
  const float em = ImGui::GetFontSize();
  const float labelColumn = em * 7.0f;
  const float fieldWidth = em * 12.0f;
  bool created = false;
  switch (*state.search()) {
  case gui_wizard::DeviceSearch::DeviceList: {
    const auto& drivers = router_.catalog().drivers();
    const std::size_t index = state.driver().value_or(0);
    if (index >= drivers.size()) {
      break;
    }
    const gui_devices::DriverEntry& entry = drivers[index];
    for (std::size_t i = 0; i < entry.devices.size(); ++i) {
      ImGui::PushID(static_cast<int>(i));
      const gui_devices::PickerRow row = gui_devices::pickerRow(router_, entry.name, entry.devices[i]);
      std::string rowText = row.genericName ? trf("{} (generic name)", row.label) : row.label;
      if (!row.selectable) {
        const std::string why = reasonWordsText(row.reason);
        rowText = why.empty() ? rowText : trf("{} - {}", rowText, why);
      }
      const bool clicked = ImGui::Selectable(
        rowText.c_str(), state.listedDevice() == i,
        ImGuiSelectableFlags_AllowDoubleClick | (row.selectable ? 0 : ImGuiSelectableFlags_Disabled));
      const std::string versionLine = driverVersionLine(
        entry.shownName(), gui_devices::pickerDriverVersion(router_, entry.name, entry.devices[i]));
      if (!row.selectable) {
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_ForTooltip)) {
          const std::string why = reasonWithDetail(row.reason, row.detail);
          ImGui::SetTooltip("%s", (why.empty() ? versionLine : why + "\n" + versionLine).c_str());
        }
      } else {
        tooltip(versionLine.c_str());
      }
      if (clicked) {
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
          if (const auto device = state.activateListItem(i)) {
            createSession(*device);
            created = true;
          }
        } else {
          state.selectListedDevice(i);
        }
      }
      ImGui::PopID();
    }
    ImGui::Spacing();
    if (entry.devices.empty()) {
      ImGui::TextDisabled("%s", driverStatus(entry).c_str());
    }
    break;
  }
  case gui_wizard::DeviceSearch::SerialPort: {
    const std::vector<std::string>& ports = state.serialPorts();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(tr("Serial port"));
    ImGui::SameLine(labelColumn);
    if (ports.empty()) {
      ImGui::TextDisabled("%s", tr("No serial port was found on this computer."));
    } else {
      const char* preview =
        state.serialPort() ? ports[*state.serialPort()].c_str() : tr("Choose port");
      ImGui::SetNextItemWidth(fieldWidth);
      if (ImGui::BeginCombo("##port", preview)) {
        for (std::size_t i = 0; i < ports.size(); ++i) {
          if (ImGui::Selectable(ports[i].c_str(), state.serialPort() == i)) {
            state.selectSerialPort(i);
          }
        }
        ImGui::EndCombo();
      }
    }
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(tr("Baud rate"));
    ImGui::SameLine(labelColumn);
    ImGui::SetNextItemWidth(fieldWidth);
    if (ImGui::BeginCombo("##baud", baudRateText(state.baudRate()).c_str())) {
      for (const std::uint32_t rate : gui_wizard::kBaudRates) {
        if (ImGui::Selectable(baudRateText(rate).c_str(), state.baudRate() == rate)) {
          state.selectBaudRate(rate);
        }
      }
      ImGui::EndCombo();
    }
    break;
  }
  case gui_wizard::DeviceSearch::Network: {
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(tr("IP address"));
    ImGui::SameLine(labelColumn);
    ImGui::SetNextItemWidth(fieldWidth);
    if (ImGui::InputTextWithHint("##address", "192.168.1.20", wizard.address.data(),
                                 wizard.address.size())) {
      state.setAddressText(wizard.address.data());
    }
    if (state.addressProblem() == gui_wizard::AddressProblem::Invalid) {
      ImGui::SameLine();
      ImGui::PushStyleColor(ImGuiCol_Text, errorTextColor(darkInUse()));
      ImGui::TextUnformatted(tr("Not IPv4 address"));
      ImGui::PopStyleColor();
    }
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(tr("Port"));
    ImGui::SameLine(labelColumn);
    ImGui::SetNextItemWidth(fieldWidth);
    if (ImGui::InputTextWithHint("##port", "5555", wizard.port.data(), wizard.port.size())) {
      state.setPortText(wizard.port.data());
    }
    if (const auto problem = state.portProblem();
        problem && *problem != gui_wizard::PortProblem::Empty) {
      ImGui::SameLine();
      ImGui::PushStyleColor(ImGuiCol_Text, errorTextColor(darkInUse()));
      ImGui::TextUnformatted(*problem == gui_wizard::PortProblem::OutOfRange
                               ? tr("Port is 1 to 65535")
                               : tr("Digits only, without leading zero"));
      ImGui::PopStyleColor();
    }
    break;
  }
  case gui_wizard::DeviceSearch::SingleDevice:
    ImGui::TextWrapped("%s", tr("This driver has one virtual device. Nothing to set up: "
                                "Create makes session."));
    ImGui::BulletText("%s", tr("Demo device"));
    break;
  }
  return created;
}

void GuiApp::drawLog() {
  if (!ImGui::Begin(withId(tr("Log"), kLogId).c_str(), &toolPanels_.logOpen)) {
    logJustShown_ = true;
    ImGui::End();
    return;
  }
  const bool justShown = logJustShown_ || ImGui::IsWindowAppearing();
  logJustShown_ = false;
  static constexpr std::array<log::LogLevel, 4> kLevels{
    log::LogLevel::debug, log::LogLevel::info, log::LogLevel::warning, log::LogLevel::error};
  std::vector<std::string> levels;
  for (log::LogLevel level : kLevels) {
    levels.emplace_back(levelLabel(level));
  }
  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 7.0f);
  comboOf(withId(tr("Level"), "level").c_str(), logMinimumLevel_, levels);
  ImGui::SameLine();
  ImGui::Checkbox(tr("Auto-scroll"), &logAutoScroll_);
  const log::LogLevel minimum = kLevels[static_cast<std::size_t>(logMinimumLevel_)];
  ImGui::SameLine();
  ImGui::TextDisabled("%s", trfn("{} line shown", "{} lines shown",
                                 static_cast<unsigned long>(logView_.shownCount(minimum)),
                                 logView_.shownCount(minimum))
                              .c_str());
  if (logView_.missed() != 0) {
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text, errorTextColor(darkInUse()));
    ImGui::TextUnformatted(trfn("missed {} line", "missed {} lines",
                                static_cast<unsigned long>(logView_.missed()), logView_.missed())
                             .c_str());
    ImGui::PopStyleColor();
  }

  const ImGuiTableFlags flags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_ScrollX |
                                ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter |
                                ImGuiTableFlags_SizingFixedFit;
  if (ImGui::BeginTable("lines", 4, flags)) {
    ImGui::TableSetupScrollFreeze(0, 0);
    ImGui::TableSetupColumn("time");
    ImGui::TableSetupColumn("level");
    ImGui::TableSetupColumn("source");
    ImGui::TableSetupColumn("message");
    std::vector<const log::LogRecord*> shown;
    for (const log::LogRecord& record : logView_.records()) {
      if (log::LogView::shown(record, minimum)) {
        shown.push_back(&record);
      }
    }
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(shown.size()));
    while (clipper.Step()) {
      for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
        const log::LogRecord& record = *shown[static_cast<std::size_t>(i)];
        const bool problem =
          record.level == log::LogLevel::error || record.level == log::LogLevel::warning;
        if (problem) {
          ImGui::PushStyleColor(ImGuiCol_Text, errorTextColor(darkInUse()));
        }
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(timeOfDay(record.timestamp).c_str());
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(levelLabel(record.level));
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(record.source.c_str());
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(record.message.c_str());
        if (problem) {
          ImGui::PopStyleColor();
        }
      }
    }
    if (gui_format::logShouldScrollToNewest(logAutoScroll_, logGrew_, justShown)) {
      ImGui::SetScrollHereY(1.0f);
    }
    ImGui::EndTable();
  }
  ImGui::End();
}

void GuiApp::drawAbout() {
  const ImVec2 centre = ImGui::GetMainViewport()->GetCenter();
  ImGui::SetNextWindowPos(centre, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  const ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings;
  if (gui_popup::beginModal("about", withId(tr("About bitoscope"), "about").c_str(), &showAbout_, flags)) {
    ImGui::PushFont(nullptr, kFontSizeBase * 1.6f);
    ImGui::TextUnformatted("bitoscope");
    ImGui::PopFont();
    ImGui::TextUnformatted(trf("Version {}", gui_format::currentVersionString()).c_str());
    ImGui::TextUnformatted(tr("Logic analyzer software: capture, view and save logic signals."));
    ImGui::Spacing();
    ImGui::SeparatorText(tr("Drivers"));
    if (!router_.catalog().driversKnown()) {
      ImGui::TextDisabled("%s", tr("Looking for drivers..."));
    } else if (router_.catalog().drivers().empty()) {
      ImGui::TextDisabled("%s", tr("No driver found"));
    }
    for (const gui_devices::DriverVersionRow& row : gui_devices::driverVersionRows(router_.catalog())) {
      versionRow(row.name, row.version);
    }
    ImGui::SeparatorText(tr("Decoder plugins"));
    const std::vector<gui_decode::DecoderPluginVersionRow> decoderRows =
      decoders_ ? gui_decode::decoderPluginVersionRows(decoders_->pools()) : std::vector<gui_decode::DecoderPluginVersionRow>{};
    if (decoderRows.empty()) {
      ImGui::TextDisabled("%s", tr("No decoder plugin found"));
    }
    for (const gui_decode::DecoderPluginVersionRow& plugin : decoderRows) {
      versionRow(plugin.plugin, plugin.version);
      ImGui::Indent();
      for (const gui_decode::DecoderVersionRow& decoder : plugin.decoders) {
        versionRow(decoder.decoder, decoder.version);
      }
      ImGui::Unindent();
    }
    ImGui::Spacing();
    ImGui::SeparatorText(tr("Third-party software"));
    const auto entry = [](const char* name, const char* license, const char* notice) {
      ImGui::BulletText("%s", name);
      ImGui::Indent();
      ImGui::TextDisabled("%s", license);
      ImGui::TextUnformatted(notice);
      ImGui::Unindent();
    };
    entry("Dear ImGui", "MIT License", "Copyright (c) 2014-2026 Omar Cornut");
    entry("plutovg", "MIT License", "Copyright (c) 2020-2026 Samuel Ugochukwu");
    entry("plutosvg", "MIT License", "Copyright (c) 2020-2026 Samuel Ugochukwu");
    entry("nlohmann/json", "MIT License", "Copyright (c) 2013-2025 Niels Lohmann");
    entry("Cap'n Proto", "MIT License",
          "Copyright (c) 2013-2017 Sandstorm Development Group, Inc.; Cloudflare, Inc.; "
          "and other contributors");
    entry("HDF5", "BSD 3-Clause License",
          "Copyright 2006 by The HDF Group. "
          "Copyright 1998-2006 by The Board of Trustees of the University of Illinois.");
    entry("zlib", "zlib License", "Copyright (C) 1995-2026 Jean-loup Gailly and Mark Adler");
    entry("LZ4", "BSD 2-Clause License", "Copyright (c) 2011-2020, Yann Collet");
    entry("LuaJIT", "MIT License",
          "Copyright (C) 2005-2026 Mike Pall. "
          "Includes code from Lua 5.1/5.2, Copyright (C) 1994-2012 Lua.org, PUC-Rio.");
    entry("GLFW (Linux)", "zlib License",
          "Copyright (c) 2002-2006 Marcus Geelnard, Copyright (c) 2006-2019 Camilla L\xC3\xB6" "wy");
    entry("Google Sans Flex", "SIL Open Font License 1.1",
          "Copyright 2022 The Google Sans Flex Project Authors "
          "(github.com/googlefonts/googlesans-flex)");
    entry("FreeType (in plutovg)", "FreeType License",
          "Portions of this software are copyright (c) 1996-2020 The FreeType Project "
          "(www.freetype.org). All rights reserved.");
    ImGui::Spacing();
    ImGui::TextDisabled("%s", trf("Dear ImGui {}", ImGui::GetVersion()).c_str());
    ImGui::EndPopup();
  }
}

void GuiApp::drawKeyboardShortcuts() {
  const ImVec2 centre = ImGui::GetMainViewport()->GetCenter();
  ImGui::SetNextWindowPos(centre, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  const ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings;
  if (gui_popup::beginModal("keyboard_shortcuts", withId(tr("Keyboard shortcuts"), "keyboard_shortcuts").c_str(),
                            &showKeyboardShortcuts_, flags)) {
    if (ImGui::BeginTable("shortcuts", 2, ImGuiTableFlags_SizingFixedFit)) {
      ImGui::TableSetupColumn(tr("Shortcut"));
      ImGui::TableSetupColumn(tr("Action"));
      ImGui::TableHeadersRow();
      const std::array<ShortcutRow, 6> rows{{
        {"F11", tr("Full screen")},
        {"Ctrl+=", tr("Zoom in")},
        {"Ctrl+-", tr("Zoom out")},
        {"Ctrl+NumPad0", tr("Reset zoom")},
        {"Delete", tr("Delete selected marker")},
        {"Escape", tr("Clear selected marker")},
      }};
      for (const ShortcutRow& row : rows) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(row.shortcut);
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(row.action);
      }
      ImGui::EndTable();
    }
    ImGui::EndPopup();
  }
}

}
