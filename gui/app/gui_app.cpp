/*
 * SPDX-License-Identifier: MIT
 */

#include "gui_app.h"

#include <device_label.h>
#include <display_text_file.h>
#include <frame_render.h>
#include <host_device_lost_adapter.h>
#include <language_choice.h>
#include <pipe_device_host.h>
#include <zoom_level.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <exception>
#include <format>
#include <thread>
#include <utility>

#include "platform_services.h"
#include "session_waveform.h"
#include "system_resources.h"
#include "tr.h"

namespace bitoscope::gui {

namespace {

constexpr std::size_t kLogCapacity = 2000;

constexpr float kIconSizeBase = 20.0f;

std::vector<std::string> iconNames() {
  std::vector<std::string> names;
  for (std::size_t i = 0; i < kIconCount; ++i) {
    names.emplace_back(iconFileName(static_cast<Icon>(i)));
  }
  return names;
}

}

GuiApp::GuiApp(PlatformServices& platform, gui_prefs::StartupPreferences startup)
    : platform_(platform),
      logger_(kLogCapacity),
      logView_(kLogCapacity),
      icons_(iconNames(),
             gui_icons::TextureCallbacks{
               [&platform](const svg_raster::RgbaImage& image)
                 -> std::expected<gui_icons::TextureHandle, std::string> {
                 auto texture = platform.createTexture(image.width, image.height, image.pixels);
                 if (!texture) {
                   return std::unexpected(texture.error());
                 }
                 return static_cast<gui_icons::TextureHandle>(*texture);
               },
               [&platform](gui_icons::TextureHandle texture) {
                 platform.destroyTexture(static_cast<ImTextureID>(texture));
               }}) {
  setTranslator(&translator_);

  if (!startup.fileFound) {
    logger_.log(log::LogLevel::info, "gui", "No preferences file yet: starting with defaults");
  } else if (startup.problem) {
    logger_.logf(log::LogLevel::warning, "gui",
                 "Preferences file could not be used, starting with defaults: {}",
                 *startup.problem);
  }
  for (const std::string& warning : startup.warnings) {
    logger_.logf(log::LogLevel::warning, "gui", "Preferences: {}", warning);
  }
  prefs_ = std::move(startup.preferences);
  themeMode_ = prefs_.themeMode;
  propertiesSide_ = prefs_.propertiesSide;
  laneNameWidthCentiEm_ = prefs_.laneNameWidthCentiEm;
  decoderPickerHeightCentiEm_ = prefs_.decoderPickerHeightCentiEm;
  fontChoice_ = prefs_.fontChoice;
  signalStyle_ = prefs_.signalStyle;
  hexStyle_ = prefs_.hexStyle;
  showDerivedRows_ = prefs_.showBits;
  showByteValues_ = prefs_.showByteValues;
  showPointerLine_ = prefs_.showPointerLine;
  lanePinSide_ = prefs_.lanePinSide;
  reduceIdleFrameRate_ = prefs_.reduceIdleFrameRate;
  zoomPercent_ = prefs_.zoomPercent;
  languagePreference_ = prefs_.language;
  toolPanels_ = prefs_.toolPanels;
  sessions_ = gui_session::SessionList::fromNames(prefs_.sessionPanels);
  restoreRemaining_ = prefs_.openSessionFiles.files;
  restoreSelect_ = gui_prefs::selectedOpenSessionFile(prefs_.openSessionFiles);
  restoreRunning_ = !restoreRemaining_.empty();
  for (const auto& session : sessions_.sessions()) {
    SessionUi& ui = sessionUi_[session.id];
    const auto saved = prefs_.sessionPanelStates.find(session.name);
    if (saved != prefs_.sessionPanelStates.end()) {
      ui.device = saved->second.device;
      ui.propertiesPanel =
        gui_session::PropertiesPanel(saved->second.propertiesOpen, saved->second.propertiesView);
      ui.propertiesWidth = static_cast<float>(saved->second.propertiesWidth.value_or(0));
      ui.lanePinSide = saved->second.lanePinSide;
      if (saved->second.deviceIdentity) {
        router_.restoreSession(session.id, *saved->second.deviceIdentity);
      }
    }
  }

  eventHandlers_.onDriversFound = [this](const std::filesystem::path& directory,
                                         const std::vector<std::string>& drivers) {
    applyRouterOutput(router_.driversFound(directory, drivers));
  };
  eventHandlers_.onDevicesListed =
    [this](std::string_view driver, const std::expected<ipc::DeviceList, std::string>& list) {
      applyRouterOutput(router_.devicesListed(driver, list));
    };
  eventHandlers_.onDeviceAppeared = [this](std::string_view driver, const ipc::DeviceInfo& device) {
    applyRouterOutput(router_.deviceAppeared(driver, device));
  };
  eventHandlers_.onDeviceLost = [this](std::string_view driver, std::string_view deviceId,
                                       std::string_view reason) {
    applyRouterOutput(router_.deviceLost(driver, deviceId, reason));
  };
  eventHandlers_.onWatchEnded = [this](std::string_view driver, std::string_view reason) {
    applyRouterOutput(router_.watchEnded(driver, reason));
  };
  eventHandlers_.onDeviceOpened =
    [this](std::string_view driver, std::string_view deviceId,
           const std::expected<host::OpenedDevice, host::OpenRefusal>& opened) {
      applyRouterOutput(router_.deviceOpened(driver, deviceId, opened));
    };
  eventHandlers_.onHostDeviceLost = [this](std::string_view pluginId, std::string_view reason) {
    applyRouterOutput(router_.hostDeviceLost(pluginId, reason));
  };
  eventHandlers_.onPluginClosed = [this](std::string_view pluginId, std::string_view reason) {
    applyRouterOutput(router_.pluginClosed(pluginId, reason));
  };
  eventHandlers_.onOpenProgress = [this](std::string_view pluginId, const ipc::OpenProgress& progress) {
    applyRouterOutput(router_.progressReported(pluginId, progress));
  };

  memoryProbe_ = makeSystemMemoryProbe();
  blockSource_ = makeSystemBlockSource();

  const std::uint32_t hardwareThreads = std::thread::hardware_concurrency();
  const std::uint32_t renderLanes = gui_waveform_frame::renderLanes(hardwareThreads);
  if (renderLanes > 1) {
    auto pool = gui_task_pool::TaskPool::create(renderLanes - 1);
    if (pool) {
      renderPool_ = std::move(*pool);
      logger_.logf(log::LogLevel::info, "gui",
                   "Waveform draws its channels on {} threads ({} hardware threads reported)",
                   renderLanes, hardwareThreads);
    } else {
      logger_.logf(log::LogLevel::warning, "gui",
                   "Waveform draws its channels on one thread: {}", pool.error());
    }
  }
  eventHandlers_.onStarted = [this](std::uint64_t run, std::uint32_t, std::string_view pluginId, std::uint64_t id) {
    gui_session::SessionId session = 0;
    if (runOwner(run, &session) != nullptr) {
      const gui_session::Session* s = sessions_.find(session);
      logger_.logf(log::LogLevel::info, "capture", "{}: started on {} (capture id {:#018x})",
                   s != nullptr ? s->name : std::to_string(session), pluginId, id);
    }
  };
  eventHandlers_.onWarning = [this](std::uint64_t run, std::uint32_t, const acquisition::AcquireWarning& warning) {
    gui_session::SessionId session = 0;
    if (runOwner(run, &session) != nullptr) {
      const gui_session::Session* s = sessions_.find(session);
      logger_.logf(log::LogLevel::warning, "capture", "{}: {}", s != nullptr ? s->name : std::to_string(session),
                   warning.text);
    }
  };
  eventHandlers_.onProgress = [this](std::uint64_t run, std::uint32_t, std::uint64_t,
                                     const acquisition::Progress& progress) {
    if (SessionUi* ui = runOwner(run)) {
      ui->capture.progress(run, progress);
    }
  };
  eventHandlers_.onRetrying = [this](std::uint64_t run, std::uint32_t, const acquisition::AcquireRetry& retry) {
    gui_session::SessionId session = 0;
    if (SessionUi* ui = runOwner(run, &session)) {
      ui->capture.retrying(run, retry);
      const gui_session::Session* s = sessions_.find(session);
      logger_.logf(log::LogLevel::warning, "capture", "{}: start was lost ({}); starting again, attempt {} of {}",
                   s != nullptr ? s->name : std::to_string(session), retry.lost, retry.attempt,
                   retry.attempts_allowed);
    }
  };
  eventHandlers_.onCaptureFinished =
    [this](std::uint64_t run, const std::expected<acquisition::SessionResult, std::string>& result) {
      captureFinished(run, result);
    };
  eventHandlers_.onDecoderResult =
    [this](std::uint64_t run, std::size_t decoderIndex,
           const std::expected<std::shared_ptr<const gui_decode::DecodedAnnotations>, std::string>& result) {
      decoderResultReceived(run, decoderIndex, result);
    };
  eventHandlers_.onDecoderProgress = [this](std::uint64_t run, std::size_t decoderIndex,
                                            const gui_decode::DecodeProgress& progress) {
    decoderProgressReceived(run, decoderIndex, progress);
  };
}

void GuiApp::startDevices() {
  const std::filesystem::path directory = platform_.executableDirectory() / "drivers";
  host::PipePluginHostOptions options;
  options.pluginDirectory = directory;
  options.startTimeout = std::chrono::seconds(30);
  options.onStderrLine = [this](std::string_view line) {
    logger_.log(log::LogLevel::debug, "plugins", line);
  };
  options.onLog = [this](std::string_view pluginId, const ipc::LogEvent& event) {
    const log::LogLevel level = event.level == ipc::LogLevel::error     ? log::LogLevel::error
                                : event.level == ipc::LogLevel::warning ? log::LogLevel::warning
                                : event.level == ipc::LogLevel::info    ? log::LogLevel::info
                                                                        : log::LogLevel::debug;
    logger_.logf(level, "plugins", "[{}] {}", pluginId, event.message);
  };
  options.onDeviceLost = gui_events::makePostingHostDeviceLost(eventQueue_);
  options.onClosed = gui_events::makePostingPluginClosed(eventQueue_);
  options.onOpenProgress = gui_events::makePostingOpenProgress(eventQueue_);
  options.onFacetEvent = [this](std::string_view pluginId, const ipc::FacetEvent& event) {
    captureEnds_.onFacetEvent(pluginId, event);
  };
  auto host = std::make_unique<gui_devices::PipeDeviceHost>(std::move(options));
  pipeHost_ = host.get();
  deviceWorker_ = std::make_unique<gui_devices::DeviceWorker>(std::move(host), eventQueue_);
  deviceWorker_->discover();
  logger_.logf(log::LogLevel::info, "devices", "Looking for drivers in {}", directory.string());
}

void GuiApp::applyRouterOutput(gui_devices::RouterOutput out) {
  for (const std::string& line : out.log) {
    logger_.log(log::LogLevel::info, "devices", line);
  }
  for (const gui_devices::DeviceRequest& request : out.open) {
    if (deviceWorker_) {
      deviceWorker_->open(request.driver, request.deviceId);
    }
  }
  for (const gui_session::Session& session : sessions_.sessions()) {
    if (!session.device) {
      continue;
    }
    const auto ui = sessionUi_.find(session.id);
    if (ui == sessionUi_.end() || !ui->second.openedFrom.empty()) {
      continue;
    }
    if (std::optional<std::string> label = gui_devices::openedDeviceLabel(router_, *session.device)) {
      ui->second.device = std::move(*label);
    }
  }
}

bool GuiApp::askDriversToListAgain() {
  const auto nowMs = static_cast<std::uint64_t>(
    std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
      .count());
  gui_devices::ListAgainStep step = gui_devices::listAgainStep(
    listAgainMemory_, gui_devices::listAgainInputs(router_.catalog(), sessions_, wizard_.has_value()), nowMs);
  listAgainMemory_ = std::move(step.memory);
  bool changed = false;
  for (const gui_devices::ListAgainRequest& request : step.requests) {
    if (deviceWorker_) {
      deviceWorker_->listAgain(request.driver);
    }
    if (request.periodic) {
      gui_devices::RouterOutput out = router_.retryWaiting(request.driver);
      changed = changed || !out.open.empty();
      applyRouterOutput(std::move(out));
    }
  }
  return changed;
}

std::string GuiApp::disabledReasonText(const gui_session::DeviceDisabledReason& reason) {
  switch (reason.kind) {
  case gui_session::DeviceReasonKind::Busy:
    return trf("Device in use by {} until its capture ends", reason.detail);
  case gui_session::DeviceReasonKind::Lost:
    return trf("Device lost: {}", reason.detail);
  case gui_session::DeviceReasonKind::Waiting:
    return trf("Waiting for device: {}", reason.detail);
  case gui_session::DeviceReasonKind::NoDevice:
    return tr("No device: make new session to choose one");
  case gui_session::DeviceReasonKind::Connecting:
    return tr("Connecting to device...");
  case gui_session::DeviceReasonKind::OpenedFromFile:
    return trf("Opened from file, with no device: {}", reason.detail);
  }
  return {};
}

GuiApp::~GuiApp() {
  shutdown();
  setTranslator(nullptr);
}

void GuiApp::configure(float dpiScale) {
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  io.ConfigWindowsMoveFromTitleBarOnly = true;
  io.IniFilename = nullptr;
  if (!prefs_.dockingLayout.empty()) {
    ImGui::LoadIniSettingsFromMemory(prefs_.dockingLayout.data(), prefs_.dockingLayout.size());
    layoutFromPreferences_ = true;
  }

  io.Fonts->AddFontDefaultVector();

  dpiScale_ = dpiScale;
  systemDark_ = platform_.systemUsesDarkTheme();
  applyStyle();
  logger_.logf(log::LogLevel::info, "gui", "Theme: {}, docking layout: {}",
               themeMode_ == gui_prefs::ThemeMode::System ? "System"
               : themeMode_ == gui_prefs::ThemeMode::Light ? "Light"
                                                           : "Dark",
               layoutFromPreferences_ ? "from preferences" : "default");

  assetDir_ = platform_.executableDirectory() / "assets";
  logger_.logf(log::LogLevel::info, "gui", "Assets: {}", assetDir_.string());
  const display_text_file::ApplyResult abbreviations = display_text_file::applyAbbreviations(
      {platform_.executableDirectory(), platform_.configDirectory(), display_text_file::abbreviationsFromEnvironment()});
  for (const std::string& summary : abbreviations.summaries) logger_.logf(log::LogLevel::info, "gui", "{}", summary);
  for (const std::string& warning : abbreviations.warnings) logger_.logf(log::LogLevel::warning, "gui", "{}", warning);
  loadIconsAndLanguage();

  // WHY: Start font load here, not in beginFrame(): a minimized or covered window begins no frame.
  startBackgroundWork();

  startDecoders();
  startDevices();
  continueRestore();
}

void GuiApp::loadIconsAndLanguage() {
  const IconAssets iconAssets = loadIcons(assetDir_, iconPixelSize(), iconColor());
  for (const std::string& problem : iconAssets.problems) {
    logger_.log(log::LogLevel::error, "gui", problem);
  }
  icons_.setSources(std::vector<std::string>(iconAssets.svg.begin(), iconAssets.svg.end()));
  for (std::size_t i = 0; i < kIconCount; ++i) {
    if (iconAssets.images[i]) {
      icons_.adopt(i, iconAssets.pixelSize, iconAssets.color, *iconAssets.images[i]);
    }
  }

  preferredLanguages_ = platform_.preferredUiLanguages();
  catalogs_ = listCatalogs(assetDir_ / kLocaleDir);
  std::string list;
  for (const std::string& tag : preferredLanguages_) {
    list += list.empty() ? tag : ", " + tag;
  }
  std::string present;
  for (const std::string& name : catalogs_) {
    present += present.empty() ? name : ", " + name;
  }
  logger_.logf(log::LogLevel::info, "gui", "Preferred UI languages: {}; translations present: {}",
               list.empty() ? "(none reported)" : list, present.empty() ? "none" : present);
  installLanguage(loadLanguage(assetDir_ / kLocaleDir, i18n::resolveLanguage(languagePreference_,
                                                                            preferredLanguages_,
                                                                            catalogs_)));
}

void GuiApp::startBackgroundWork() {
  fontLoad_ = std::async(std::launch::async, [&platform = platform_, assetDir = assetDir_,
                                              choice = fontChoice_]() -> FontAsset {
    if (choice == gui_prefs::FontChoice::GoogleSansFlex) {
      return loadFont(assetDir);
    }
    const std::optional<std::filesystem::path> systemFont = platform.systemFontFile();
    if (!systemFont) {
      FontAsset fallback = loadFont(assetDir);
      fallback.problem = "System font: not found; using Google Sans Flex instead";
      return fallback;
    }
    FontAsset asset =
      loadFontFile(*systemFont, std::format("System ({})", systemFont->filename().string()));
    if (!asset.bytes.empty()) {
      return asset;
    }
    FontAsset fallback = loadFont(assetDir);
    fallback.problem =
      std::format("{}; using Google Sans Flex instead", asset.problem.value_or("System font: not loaded"));
    return fallback;
  });
}

void GuiApp::setDpiScale(float dpiScale) {
  if (dpiScale != dpiScale_) {
    dpiScale_ = dpiScale;
    styleDirty_ = true;
  }
}

void GuiApp::systemThemeChanged() {
  const bool dark = platform_.systemUsesDarkTheme();
  if (dark != systemDark_) {
    systemDark_ = dark;
    logger_.logf(log::LogLevel::info, "gui", "System theme is now {}", dark ? "dark" : "light");
    if (themeMode_ == gui_prefs::ThemeMode::System) {
      styleDirty_ = true;
    }
  }
}

bool GuiApp::darkInUse() const {
  return gui_prefs::resolveTheme(themeMode_, systemDark_) == gui_prefs::Theme::Dark;
}

svg_raster::Rgba8 GuiApp::iconColor() const {
  const ImVec4& c = ImGui::GetStyle().Colors[ImGuiCol_Text];
  const auto channel = [](float v) { return static_cast<std::uint8_t>(std::lround(v * 255.0f)); };
  return {channel(c.x), channel(c.y), channel(c.z), channel(c.w)};
}

std::uint32_t GuiApp::iconPixelSize() const {
  return static_cast<std::uint32_t>(std::lround(kIconSizeBase * uiScale()));
}

void GuiApp::applyStyle() {
  ImGui::GetStyle() = makeStyle(darkInUse(), uiScale());
  platform_.setWindowFrameDark(darkInUse());
  icons_.clear();
  styleDirty_ = false;
}

void GuiApp::setThemeMode(gui_prefs::ThemeMode mode) {
  if (mode == themeMode_) {
    return;
  }
  themeMode_ = mode;
  styleDirty_ = true;
  savePreferencesNow();
}

void GuiApp::setPropertiesSide(gui_prefs::PropertiesSide side) {
  if (side == propertiesSide_) {
    return;
  }
  propertiesSide_ = side;
  savePreferencesNow();
}

void GuiApp::toggleFullScreen() {
  const bool entering = !platform_.isFullScreen();
  platform_.setFullScreen(entering);
  logger_.log(log::LogLevel::info, "gui",
             entering ? "Entered full screen" : "Left full screen");
  savePreferencesNow();
}

void GuiApp::chooseFontChoice(gui_prefs::FontChoice choice) {
  if (choice == fontChoice_) {
    return;
  }
  fontChoice_ = choice;
  savePreferencesNow();
  fontLoadDone_ = false;
  startBackgroundWork();
}

void GuiApp::setSignalStyle(gui_prefs::SignalStyle style) {
  if (style == signalStyle_) {
    return;
  }
  signalStyle_ = style;
  savePreferencesNow();
}

void GuiApp::setHexStyle(decode::HexStyle style) {
  if (style == hexStyle_) {
    return;
  }
  hexStyle_ = style;
  savePreferencesNow();
}

void GuiApp::setShowBits(bool show) {
  if (show == showDerivedRows_) {
    return;
  }
  showDerivedRows_ = show;
  savePreferencesNow();
}

void GuiApp::setShowByteValues(bool show) {
  if (show == showByteValues_) {
    return;
  }
  showByteValues_ = show;
  savePreferencesNow();
}

void GuiApp::setShowPointerLine(bool show) {
  if (show == showPointerLine_) {
    return;
  }
  showPointerLine_ = show;
  savePreferencesNow();
}

float GuiApp::zoomFactor() const {
  return gui_prefs::zoomFactor(zoomPercent_);
}

void GuiApp::setZoomPercent(std::int32_t percent) {
  const std::int32_t clamped = gui_prefs::clampZoomPercent(percent);
  if (clamped == zoomPercent_) {
    return;
  }
  zoomPercent_ = clamped;
  styleDirty_ = true;
  savePreferencesNow();
}

void GuiApp::zoomIn() {
  setZoomPercent(gui_prefs::zoomIn(zoomPercent_));
}

void GuiApp::zoomOut() {
  setZoomPercent(gui_prefs::zoomOut(zoomPercent_));
}

void GuiApp::resetZoom() {
  setZoomPercent(gui_prefs::kZoomDefaultPercent);
}

void GuiApp::beginFrame() {
  if (styleDirty_) {
    applyStyle();
  }
  pumpBackground();
  logGrew_ = logGrewSinceFrame_;
  logGrewSinceFrame_ = false;
  if (focusDelayFrames_ > 0) {
    --focusDelayFrames_;
  }
}

void GuiApp::setWakeCallback(std::function<void()> wake) {
  eventQueue_.setWakeCallback(std::move(wake));
}

void GuiApp::setReduceIdleFrameRate(bool reduce) {
  if (reduce == reduceIdleFrameRate_) {
    return;
  }
  reduceIdleFrameRate_ = reduce;
  savePreferencesNow();
}

bool GuiApp::captureRunning() const {
  for (const auto& [id, ui] : sessionUi_) {
    if (ui.runner) {
      return true;
    }
  }
  return false;
}

std::string GuiApp::windowTitle(std::string_view base) const {
  return gui_capture::windowTitleText(base, gui_capture::windowUnsavedMark(closingPanels()));
}

bool GuiApp::captureAdvanced() {
  bool advanced = false;
  for (auto& [id, ui] : sessionUi_) {
    if (!ui.runner) {
      continue;
    }
    if (!ui.waveform || ui.waveform->sources.snapshotAdvanced()) {
      advanced = true;
    }
  }
  return advanced;
}

std::vector<GuiApp::PanelCaptureStatus> GuiApp::panelCaptureStatuses() const {
  std::vector<PanelCaptureStatus> out;
  for (const auto& session : sessions_.sessions()) {
    const auto ui = sessionUi_.find(session.id);
    if (ui != sessionUi_.end()) {
      out.push_back(PanelCaptureStatus{session.name, gui_capture::captureStatusOf(ui->second.capture)});
    }
  }
  return out;
}

bool GuiApp::asyncWorkInFlight() const {
  // WHY: Work ending in a std::future wakes no loop, so the loop polls while any is pending; the event queue wakes it.
  return (!fontLoadDone_ && fontLoad_.valid()) || languageLoad_.valid() || !pendingSaves_.empty() ||
         !pendingViewSaves_.empty() || !pendingOpens_.empty() || fileRequest_.has_value();
}

bool GuiApp::wantsMoreFrames() const {
  if (ImGui::GetCurrentContext() == nullptr) {
    return false;
  }
  const ImGuiIO& io = ImGui::GetIO();
  for (const bool down : io.MouseDown) {
    if (down) {
      return true;
    }
  }
  return ImGui::IsAnyItemActive();
}

bool GuiApp::pumpBackground() {
  bool changed = false;
  if (!fontLoadDone_ && fontLoad_.valid() &&
      fontLoad_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
    changed = true;
    fontLoadDone_ = true;
    try {
      FontAsset font = fontLoad_.get();
      if (font.problem) {
        logger_.log(font.bytes.empty() ? log::LogLevel::error : log::LogLevel::warning, "gui",
                   *font.problem);
      }
      if (!font.bytes.empty()) {
        installFont(font);
      }
    } catch (const std::exception& e) {
      logger_.logf(log::LogLevel::error, "gui", "Font load failed: {}", e.what());
    }
  }
  if (languageLoad_.valid() &&
      languageLoad_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
    changed = true;
    try {
      installLanguage(languageLoad_.get());
    } catch (const std::exception& e) {
      logger_.logf(log::LogLevel::error, "gui", "Loading language failed: {}", e.what());
    }
  }
  for (const std::string& problem : icons_.takeProblems()) {
    logger_.log(log::LogLevel::error, "gui", problem);
  }
  if (WaveformRenderer* renderer = platform_.waveformRenderer()) {
    for (const std::string& problem : renderer->takeProblems()) {
      logger_.log(log::LogLevel::error, "gui", problem);
    }
  }
  if (eventQueue_.drain(eventHandlers_) != 0) {
    changed = true;
  }
  followLiveDecodes();
  if (askDriversToListAgain()) {
    changed = true;
  }
  if (fileRequest_) {
    runFileRequest();
    changed = true;
  }
  if (pollFileWork()) {
    changed = true;
  }
  if (closeFlow_.active() && advanceCloseFlow()) {
    changed = true;
  }
  if (logView_.update(logger_.ring()) != 0) {
    logGrewSinceFrame_ = true;
    changed = true;
  }
  return changed;
}

void GuiApp::installLanguage(LoadedLanguage loaded) {
  if (loaded.problem) {
    logger_.log(log::LogLevel::warning, "gui", *loaded.problem);
  }
  translator_.install(std::move(loaded.catalog), loaded.language);
  logger_.logf(log::LogLevel::info, "gui", "Language: {}{}", loaded.language,
               languagePreference_.empty() ? " (following system)" : "");
}

void GuiApp::chooseLanguage(std::string preference) {
  languagePreference_ = std::move(preference);
  savePreferencesNow();
  followLanguagePreference();
}

void GuiApp::followLanguagePreference() {
  const std::string language =
    i18n::resolveLanguage(languagePreference_, preferredLanguages_, catalogs_);
  if (language == translator_.language() && !languageLoad_.valid()) {
    return;
  }
  languageLoad_ = std::async(std::launch::async, [localeDir = assetDir_ / kLocaleDir, language] {
    return loadLanguage(localeDir, language);
  });
}

void GuiApp::installFont(const FontAsset& font) {
  ImGuiIO& io = ImGui::GetIO();
  const std::vector<std::uint8_t>& ttf = font.bytes;
  // WHY: Dear ImGui keeps font bytes while the font exists and takes ownership of a copy made with IM_ALLOC.
  void* data = IM_ALLOC(ttf.size());
  std::memcpy(data, ttf.data(), ttf.size());
  ImFontConfig config;
  config.FontDataOwnedByAtlas = true;
  std::snprintf(config.Name, sizeof(config.Name), "%s", font.label.c_str());
  // WHY: An unparsable font is a Dear ImGui user error that asserts in debug: disable the assert around the load.
  const bool assertOnError = io.ConfigErrorRecoveryEnableAssert;
  io.ConfigErrorRecoveryEnableAssert = false;
  ImFont* loaded = io.Fonts->AddFontFromMemoryTTF(data, static_cast<int>(ttf.size()), kFontSizeBase,
                                                  &config);
  io.ConfigErrorRecoveryEnableAssert = assertOnError;
  if (loaded == nullptr) {
    logger_.logf(log::LogLevel::error, "gui",
                "Font not loaded: Dear ImGui could not parse {}; keeping current font", font.label);
    return;
  }
  ImFontConfig mergeConfig;
  mergeConfig.MergeMode = true;
  mergeConfig.SizePixels = kFontSizeBase;
  io.Fonts->AddFontDefaultVector(&mergeConfig);
  io.FontDefault = loaded;
  // WHY: Remove the previously installed font: switching back and forth would grow the atlas by a font file each time.
  if (installedFont_ != nullptr) {
    io.Fonts->RemoveFont(installedFont_);
  }
  installedFont_ = loaded;
  ++fontSerial_;
  logger_.logf(log::LogLevel::info, "gui", "Font: {}", font.label);
}

std::filesystem::path GuiApp::preferencesPath() { return platform_.preferencesFile(); }

gui_prefs::Preferences GuiApp::currentPreferences() {
  gui_prefs::Preferences prefs = prefs_;
  prefs.formatVersion = gui_prefs::kCurrentFormatVersion;
  prefs.themeMode = themeMode_;
  prefs.propertiesSide = propertiesSide_;
  prefs.laneNameWidthCentiEm = laneNameWidthCentiEm_;
  prefs.decoderPickerHeightCentiEm = decoderPickerHeightCentiEm_;
  prefs.fullScreen = platform_.isFullScreen();
  prefs.fontChoice = fontChoice_;
  prefs.signalStyle = signalStyle_;
  prefs.hexStyle = hexStyle_;
  prefs.showBits = showDerivedRows_;
  prefs.showByteValues = showByteValues_;
  prefs.showPointerLine = showPointerLine_;
  prefs.lanePinSide = lanePinSide_;
  prefs.reduceIdleFrameRate = reduceIdleFrameRate_;
  prefs.zoomPercent = zoomPercent_;
  prefs.language = languagePreference_;
  if (auto placement = platform_.windowPlacement()) {
    // WHY: Placement is the pre-full-screen one; touch only this window system's key, leave other entries as loaded.
    prefs.windowPlacements[platform_.windowSystem()] = *placement;
  }
  prefs.toolPanels = toolPanels_;
  prefs.sessionPanels = sessions_.persistentNames();
  prefs.sessionPanelStates.clear();
  std::vector<std::string> tabFiles;
  for (const auto& session : sessions_.sessions()) {
    tabFiles.push_back(sessionFileOf(session.id));
  }
  const std::optional<gui_session::SessionId> activeTab = sessions_.active();
  const std::string selectedFile = restoreRunning_ && restoreSelect_ ? *restoreSelect_
                                   : activeTab                      ? sessionFileOf(*activeTab)
                                                                    : std::string();
  std::vector<std::string> pendingFiles = restoreRemaining_;
  for (std::string& file : filesBeingOpened()) {
    pendingFiles.push_back(std::move(file));
  }
  prefs.openSessionFiles = gui_prefs::openSessionFilesFor(tabFiles, pendingFiles, selectedFile);
  for (const auto& session : sessions_.sessions()) {
    const auto ui = sessionUi_.find(session.id);
    if (ui == sessionUi_.end() || !session.openedFrom.empty()) {
      continue;
    }
    gui_prefs::SessionPanelState state;
    state.device = ui->second.device;
    if (const gui_session::DeviceIdentity* device = sessions_.device(session.id)) {
      state.deviceIdentity = *device;
    }
    state.propertiesOpen = ui->second.propertiesPanel.isShown();
    state.propertiesView = ui->second.propertiesPanel.selectedView();
    if (ui->second.propertiesWidth >= 1.0f) {
      state.propertiesWidth = static_cast<std::int32_t>(std::lround(ui->second.propertiesWidth));
    }
    state.lanePinSide = ui->second.lanePinSide;
    prefs.sessionPanelStates.emplace(session.name, std::move(state));
  }
  std::size_t size = 0;
  const char* ini = ImGui::SaveIniSettingsToMemory(&size);
  prefs.dockingLayout.assign(ini, size);
  return prefs;
}

void GuiApp::savePreferencesNow() {
  const std::filesystem::path path = preferencesPath();
  if (path.empty()) {
    logger_.log(log::LogLevel::warning, "gui",
                "Preferences were not saved: platform named no configuration directory");
    return;
  }
  prefs_ = currentPreferences();
  if (auto saved = gui_prefs::savePreferences(path, prefs_); !saved) {
    logger_.logf(log::LogLevel::error, "gui", "Preferences were not saved: {}", saved.error());
    return;
  }
  logger_.logf(log::LogLevel::info, "gui", "Preferences saved to {}", path.string());
}

void GuiApp::status(std::string message) {
  logger_.logf(log::LogLevel::info, "gui", "{}", message);
}

std::array<float, 4> GuiApp::clearColor() const {
  const ImVec4& c = ImGui::GetStyle().Colors[ImGuiCol_WindowBg];
  return {c.x, c.y, c.z, 1.0f};
}

void GuiApp::shutdown() {
  finishFileWork();
  if (!saved_) {
    saved_ = true;
    savePreferencesNow();
  }
  stopAllCaptures();
  endDecoders();
  // WHY: Switch generator outputs off only AFTER stopAllCaptures(): a command to a capturing device wedged it.
  constexpr bool switchOff = true;
  for (auto& [id, ui] : sessionUi_) {
    syncGeneratorOutputs(ui, switchOff);
  }
  if (deviceWorker_) {
    deviceWorker_->shutdown();
    deviceWorker_.reset();
    pipeHost_ = nullptr;
  }
  if (fontLoad_.valid()) {
    fontLoad_.wait();
    fontLoad_ = {};
  }
  if (languageLoad_.valid()) {
    languageLoad_.wait();
    languageLoad_ = {};
  }
  for (auto& [id, ui] : sessionUi_) {
    retireWaveform(ui);
  }
  icons_.clear();
}

}
