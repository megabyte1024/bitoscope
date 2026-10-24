/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <imgui.h>

#include <capture_end_recorder.h>
#include <capture_runner.h>
#include <capture_settings.h>
#include <capture_status.h>
#include <close_flow.h>
#include <color_choice.h>
#include <device_router.h>
#include <device_worker.h>
#include <frame_probe.h>
#include <generator_facet.h>
#include <generator_switch_off.h>
#include <gui_event_handlers.h>
#include <gui_event_queue.h>
#include <icon_cache.h>
#include <interaction.h>
#include <lane_display.h>
#include <lane_order.h>
#include <list_again.h>
#include <annotation_store.h>
#include <decode_signal_view.h>
#include <decoder.h>
#include <decoder_config.h>
#include <decoder_lane_model.h>
#include <find_matches.h>
#include <lane_picture_cache.h>
#include <decoder_registry.h>
#include <decoder_runner.h>
#include <gui_decoders.h>
#include <option_text_buffers.h>
#include <live_capture_slot.h>
#include <log_view.h>
#include <logger.h>
#include <memory_probe.h>
#include <new_session_wizard.h>
#include <number_format.h>
#include <panel_capture.h>
#include <preferences.h>
#include <task_pool.h>
#include <preferences_file.h>
#include <properties_layout.h>
#include <properties_panel.h>
#include <save_command.h>
#include <property_editor_model.h>
#include <property_tree_layout.h>
#include <session_list.h>
#include <session_load.h>
#include <session_save.h>
#include <start_flow.h>
#include <translator.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "assets.h"
#include "theme.h"

namespace bitoscope::gui_devices {
class PipeDeviceHost;
}

namespace bitoscope::gui {

class PlatformServices;
struct SessionWaveform;

class GuiApp {
 public:
  GuiApp(PlatformServices& platform, gui_prefs::StartupPreferences startup);
  ~GuiApp();
  GuiApp(const GuiApp&) = delete;
  GuiApp& operator=(const GuiApp&) = delete;

  void configure(float dpiScale);

  void setDpiScale(float dpiScale);

  void systemThemeChanged();

  void beginFrame();

  bool pumpBackground();

  bool captureRunning() const;
  bool asyncWorkInFlight() const;
  bool wantsMoreFrames() const;
  bool reduceIdleFrameRate() const { return reduceIdleFrameRate_; }

  void setWakeCallback(std::function<void()> wake);

  void drawFrame();

  void setFrameProbe(gui_frame_probe::FrameProbe* probe) { frameProbe_ = probe; }

  std::array<float, 4> clearColor() const;

  void requestAppClose();
  bool closeConfirmed() const { return closeConfirmed_; }

  void shutdown();

  log::Logger& logger() { return logger_; }

  struct PanelCaptureStatus {
    std::string name;
    gui_capture::CaptureStatus status;
  };
  std::vector<PanelCaptureStatus> panelCaptureStatuses() const;

 private:
  struct SessionUi {
    std::string device;
    gui_session::PropertiesPanel propertiesPanel;
    float propertiesWidth = 0;
    float splitterGrabOffset = 0;
    std::shared_ptr<SessionWaveform> waveform;

    std::string capabilitiesKey;
    std::optional<host::pipe::LaCapabilities> capabilities;
    std::string capabilitiesProblem;
    std::optional<gui_capture::CaptureSettings> settings;
    std::optional<gui_properties::DevicePropertiesModel> deviceProperties;
    std::vector<gui_properties::GeneratorSwitchOff> generatorSwitchOff;
    std::map<std::string, std::array<char, 96>> propertyTextBuffers;
    std::vector<waveform_view::ColorChoice> channelColors;
    std::vector<waveform_view::ColorChoice> channelDefaultColors;
    std::vector<std::string> channelNames;
    std::vector<std::string> channelUserNames;

    gui_capture::PanelCapture capture;
    gui_capture::StartFlow startFlow;
    std::array<char, 40> lengthText{};
    bool lengthEditing = false;
    bool rememberUnsavedChoice = false;
    std::optional<gui_capture::CaptureRequest> startRequest;
    std::optional<gui_capture::Alternatives> alternatives;
    std::optional<gui_capture::SettingsProblem> settingsProblem;
    std::unique_ptr<gui_capture::CaptureRunner> runner;
    std::shared_ptr<gui_capture::LiveCaptureSlot> slot;
    std::optional<gui_capture::CaptureRequest> running;
    std::vector<std::uint32_t> keptDeviceChannels;
    std::vector<bool> fileLanesShown;

    std::chrono::system_clock::time_point capturedAt{};
    std::vector<capture::SavedProperty> keptDeviceProperties;
    std::string openedFrom;
    std::string openedInfo;

    std::vector<gui_decode::DecoderConfig> decoders;
    std::vector<std::pair<std::string, std::string>> loadedDecoderSetup;
    std::vector<std::size_t> loadedDecoderSkipped;
    gui_decode::AnnotationStore decoderAnnotations;
    gui_decode::DecoderLaneLabelCache laneLabels;
    std::vector<gui_decode::LanePictureCache> lanePictures;
    std::uint64_t laneViewSerial = 0;
    std::optional<capture::RawBufferSignalView> laneView;
    std::unique_ptr<gui_decode::DecoderRunner> decoderRunner;
    std::string addDecoderPick;
    std::uint32_t nextDecoderColor = 0;
    std::array<char, 64> decoderFilter{};
    gui_decode::OptionTextBuffers decoderOptionBuffers;
    std::vector<std::string> loadedDecoderPresentation;
    std::vector<capture::DecoderVersions> loadedDecoderVersions;
    std::map<std::string, std::string, std::less<>> decoderFieldMessages;

    waveform_view::LaneOrder laneOrder;
    std::optional<waveform_view::PinSide> lanePinSide;
  };

  struct Wizard {
    gui_wizard::NewSessionWizard state;
    std::array<char, 64> address{};
    std::array<char, 16> port{};
    bool popupAbove = false;
    bool driversKnown = false;
  };

  bool darkInUse() const;
  svg_raster::Rgba8 iconColor() const;
  std::uint32_t iconPixelSize() const;
  void applyStyle();
  void loadIconsAndLanguage();
  void startBackgroundWork();
  void installFont(const FontAsset& font);
  void installLanguage(LoadedLanguage loaded);
  void chooseLanguage(std::string preference);
  void followLanguagePreference();
  void setThemeMode(gui_prefs::ThemeMode mode);
  void setPropertiesSide(gui_prefs::PropertiesSide side);
  void toggleFullScreen();
  void chooseFontChoice(gui_prefs::FontChoice choice);
  void setSignalStyle(gui_prefs::SignalStyle style);
  void setHexStyle(decode::HexStyle style);
  void setShowBits(bool show);
  void setShowByteValues(bool show);
  void setShowPointerLine(bool show);
  void setReduceIdleFrameRate(bool reduce);

  void zoomIn();
  void zoomOut();
  void resetZoom();
  void setZoomPercent(std::int32_t percent);
  float zoomFactor() const;
  float uiScale() const { return dpiScale_ * zoomFactor(); }
  std::filesystem::path preferencesPath();
  gui_prefs::Preferences currentPreferences();
  void savePreferencesNow();
  void openNewSessionWizard();
  void createSession(const gui_wizard::NewDevice& device);
  void createSessionFor(const std::string& driver, const ipc::DeviceInfo& device);
  void status(std::string message);

  void startDevices();
  void applyRouterOutput(gui_devices::RouterOutput out);
  bool askDriversToListAgain();
  static std::string disabledReasonText(const gui_session::DeviceDisabledReason& reason);

  void syncCaptureSettings(const gui_session::Session& session, SessionUi& ui);
  void pressStart(gui_session::SessionId id);
  void pressStop(gui_session::SessionId id);
  void advanceStartFlow(gui_session::SessionId id, SessionUi& ui);
  std::expected<acquisition::MemoryCheck, std::string> checkMemory(const gui_capture::CaptureRequest& request);
  void startCaptureRun(gui_session::SessionId id, SessionUi& ui);
  SessionUi* runOwner(std::uint64_t run, gui_session::SessionId* id = nullptr);
  void captureFinished(std::uint64_t run, const std::expected<acquisition::SessionResult, std::string>& result);
  void stopAllCaptures();
  std::string settingsProblemText(const gui_capture::SettingsProblem& problem) const;
  void drawStartModals(const gui_session::Session& session, SessionUi& ui);
  std::vector<gui_capture::ClosingPanel> closingPanels() const;
  gui_capture::UnsavedPolicy rememberedUnsavedPolicy() const;
  void requestPanelClose(gui_session::SessionId id);
  bool advanceCloseFlow();
  void drawCloseModal();
  void closeSession(gui_session::SessionId id);
  void drawCaptureStatus(SessionUi& ui);
  std::string captureStatusText(gui_session::SessionId id);
  void rateCombo(SessionUi& ui, const char* id);
  void lengthCombo(SessionUi& ui, const char* id);

  void requestSave(gui_session::SessionId id, bool thenStart,
                   gui_session_file::SaveCommand command);
  void requestOpen();
  void openFile(const std::filesystem::path& path, bool restore = false);
  void continueRestore();
  void finishRestore();
  void runFileRequest();
  bool pollFileWork();
  void finishFileWork();
  void saveTo(gui_session::SessionId id, const std::filesystem::path& path, bool thenStart);
  void startSave(gui_session::SessionId id, const std::filesystem::path& path, bool thenStart);
  void saveFinished(gui_session::SessionId id, const std::filesystem::path& path, bool thenStart,
                    const std::expected<void, std::string>& result);
  struct InPlaceSaveResult {
    std::expected<void, std::string> view;
    std::optional<std::expected<void, std::string>> decoders;
  };
  void startViewSave(gui_session::SessionId id, const gui_capture::PanelCapture::KnownFile& file, bool thenStart);
  void viewSaveFinished(gui_session::SessionId id, bool thenStart, const InPlaceSaveResult& result,
                        const std::optional<gui_session_file::DecoderUpdate>& decoders,
                        const std::vector<std::pair<std::string, std::string>>& decodersAtStart,
                        const std::vector<std::string>& presentationAtStart);
  void opened(const std::filesystem::path& path,
              std::expected<std::vector<gui_session_file::LoadedPanel>, std::string> result, bool restore);
  void noteRecentFile(const std::filesystem::path& path);
  void rememberFile(const std::filesystem::path& path);
  std::string sessionFileOf(gui_session::SessionId id) const;
  std::vector<std::string> filesBeingOpened() const;
  static std::string saveBlockedText(gui_capture::PanelCapture::SaveBlocked why);
  void drawOpenRecentMenu();
  void drawFileErrorModal();
  void drawReplaceModal();

  void syncWaveform(gui_session::SessionId id, SessionUi& ui, const gui_capture::LiveCaptureSlot::View* live);
  void dropWaveformCapture(SessionUi& ui);
  void retireWaveform(SessionUi& ui);
  void waveformCommand(SessionUi& ui, waveform_view::ViewCommand command, double anchorPixelX);
  void applyLaneAction(SessionUi& ui, const waveform_view::LaneActionResult& result);
  void resetLaneOrder(SessionUi& ui);
  static bool hasLanes(const SessionUi& ui);

  void handleZoomShortcuts();
  SessionWaveform* activeWaveform();
  void openFind(SessionWaveform& waveform);
  void stepFind(SessionWaveform& waveform, bool forward);
  void handleFindShortcuts();
  std::optional<gui_decode::FindMatch> updateFind(SessionUi& ui, const waveform_view::LaneDisplay& lanes,
                                                  std::span<const gui_decode::DecoderLaneModel> decoderLanes,
                                                  std::uint64_t anchor);
  void drawFindBox(SessionWaveform& waveform, float right, float top, bool dark);
  std::uint32_t iconTexturePixels(std::uint32_t layoutPixels) const;
  bool iconButton(const char* id, Icon icon, const char* tooltip, bool mirrored = false,
                  bool selected = false);
  bool iconButtonSized(const char* id, Icon icon, std::uint32_t pixelSize, const char* tooltip,
                       bool mirrored = false, bool selected = false);
  bool iconTextButton(const char* id, Icon icon, const char* label);
  void drawMenuBar();
  void drawStatusBar();
  void buildDefaultLayout(ImGuiID dockspace);
  void dockLogAtBottom(ImGuiID dockspace);
  void drawNoSessionHint();
  void drawSession(const gui_session::Session& session, ImGuiID centralNode);
  void drawSessionToolbar(const gui_session::Session& session, SessionUi& ui);
  void drawSessionBody(const gui_session::Session& session, SessionUi& ui);
  void drawWaveformToolbar(SessionUi& ui);
  void drawWaveform(gui_session::SessionId sessionId, SessionUi& ui, float headerWidth, float width,
                    float tracesMinimum);
  void drawPropertiesActivityBar(SessionUi& ui, float width);
  void drawDeviceProperties(SessionUi& ui);
  void drawPropertyFacetSection(SessionUi& ui, const std::string& facetName,
                                gui_properties::FacetPropertiesModel& model, bool capturing, bool withNode);
  void drawPropertySection(SessionUi& ui, const char* title, const std::vector<std::string>& facets, bool capturing);
  void drawGeneratorSwitchOffProblems(SessionUi& ui);
  void resetFacetToDefault(SessionUi& ui, const std::string& facetName, gui_properties::FacetPropertiesModel& model,
                           bool capturing);
  void drawPropertyFacet(SessionUi& ui, const std::string& facetName, gui_properties::FacetPropertiesModel& model,
                         bool capturing);
  void drawPropertyChildren(SessionUi& ui, const std::string& facetName, gui_properties::FacetPropertiesModel& model,
                            const std::vector<gui_properties::PropertyTreeNode>& children, bool capturing);
  void drawPropertyRow(SessionUi& ui, const std::string& facetName, gui_properties::FacetPropertiesModel& model,
                       const gui_properties::PropertyDisplay& property, bool capturing,
                       gui_format::PropertyTreeColumns columns);
  void sendPropertyNow(const SessionUi& ui, const std::string& facetName, const gui_properties::PropertyValue& value);
  void sendFacetPropertiesNow(const SessionUi& ui, const std::string& facetName,
                              const std::vector<gui_properties::PropertyValue>& values);
  void syncGeneratorOutputs(SessionUi& ui, bool switchOff);

  const decode::DecoderRegistry& decoderRegistry() const;
  void startDecoders();
  void endDecoders();
  static std::vector<gui_decode::CapturedChannel> capturedChannelsFor(const SessionUi& ui);
  void runDecodersForSession(gui_session::SessionId id, SessionUi& ui);
  void decodeSessionAgain(gui_session::SessionId id, SessionUi& ui);
  void startDecodeRun(gui_session::SessionId id, SessionUi& ui);
  SessionUi* decodeRunOwner(std::uint64_t run);
  void letGoDecodes(SessionUi& ui);
  void decoderResultReceived(std::uint64_t run, std::size_t decoderIndex,
                             const std::expected<std::shared_ptr<const gui_decode::DecodedAnnotations>, std::string>& result);
  void decoderProgressReceived(std::uint64_t run, std::size_t decoderIndex, const gui_decode::DecodeProgress& progress);
  void drawDecodersProperties(SessionUi& ui);
  bool drawDecoderPicker(SessionUi& ui);
  struct DecoderChannelChoices {
    std::vector<gui_decode::CapturedChannel> captured;
    std::vector<std::string> labels;
  };
  static DecoderChannelChoices decoderChannelChoices(const SessionUi& ui);
  struct DecoderSettingsChange {
    bool setup = false;
    bool presentation = false;
  };
  DecoderSettingsChange drawDecoderSettings(SessionUi& ui, std::size_t index, const DecoderChannelChoices& channels);
  void applyDecoderSettingsChange(gui_session::SessionId id, SessionUi& ui, const DecoderSettingsChange& change);
  void drawChannelSelector(std::vector<bool>& selected, const std::vector<waveform_view::ColorChoice>& colors,
                           const std::vector<std::uint32_t>& connected, const SessionWaveform* shown,
                           const std::vector<std::uint32_t>& entryChannel = {});
  void drawNewSessionWizard();
  bool drawWizardDevicePage(Wizard& wizard);
  void drawLog();
  void drawAbout();
  void drawKeyboardShortcuts();

  PlatformServices& platform_;
  gui_frame_probe::FrameProbe* frameProbe_ = nullptr;
  log::Logger logger_;
  log::LogView logView_;
  i18n::Translator translator_;

  gui_events::GuiEventQueue eventQueue_;
  gui_events::GuiEventHandlers eventHandlers_;

  std::unique_ptr<gui_decode::GuiDecoders> decoders_;

  gui_prefs::Preferences prefs_;
  bool saved_ = false;

  float dpiScale_ = 1.0f;
  gui_prefs::ThemeMode themeMode_ = gui_prefs::ThemeMode::System;
  gui_prefs::PropertiesSide propertiesSide_ = gui_prefs::PropertiesSide::Left;
  std::int32_t laneNameWidthCentiEm_ = gui_prefs::kLaneNameWidthDefaultCentiEm;
  std::int32_t decoderPickerHeightCentiEm_ = gui_prefs::kPickerHeightDefaultCentiEm;
  float laneDividerPressX_ = 0.0f;
  std::int32_t laneDividerPressCentiEm_ = gui_prefs::kLaneNameWidthDefaultCentiEm;
  gui_prefs::FontChoice fontChoice_ = gui_prefs::FontChoice::System;
  gui_prefs::SignalStyle signalStyle_ = gui_prefs::SignalStyle::LevelColors;
  decode::HexStyle hexStyle_ = decode::HexStyle::Suffixed;
  bool showDerivedRows_ = true;
  bool showByteValues_ = true;
  bool showPointerLine_ = true;
  waveform_view::PinSide lanePinSide_ = waveform_view::PinSide::Bottom;

  std::int32_t zoomPercent_ = gui_prefs::kZoomDefaultPercent;
  bool systemDark_ = false;
  bool styleDirty_ = false;

  std::future<FontAsset> fontLoad_;
  bool fontLoadDone_ = false;
  ImFont* installedFont_ = nullptr;
  std::uint64_t fontSerial_ = 0;
  std::filesystem::path assetDir_;
  std::vector<std::string> preferredLanguages_;
  std::vector<std::string> catalogs_;
  std::string languagePreference_;
  std::future<LoadedLanguage> languageLoad_;

  gui_icons::IconCache icons_;

  gui_session::SessionList sessions_;
  gui_devices::DeviceRouter router_{sessions_};
  std::map<gui_session::SessionId, SessionUi> sessionUi_;
  gui_prefs::ToolPanels toolPanels_;
  bool layoutFromPreferences_ = false;
  bool resetLayout_ = false;
  bool dockLogAtBottom_ = false;
  bool layoutBuilt_ = false;
  bool showAbout_ = false;
  bool showKeyboardShortcuts_ = false;

  gui_capture::CaptureEndRecorder captureEnds_;
  std::uint64_t nextRun_ = 1;
  std::unique_ptr<capture::MemoryProbe> memoryProbe_;
  std::unique_ptr<capture::BlockSource> blockSource_;

  std::unique_ptr<gui_task_pool::TaskPool> renderPool_;

  struct FileRequest {
    enum class Kind { Save, Open };
    Kind kind = Kind::Save;
    gui_session::SessionId session = 0;
    bool thenStart = false;
  };
  std::optional<FileRequest> fileRequest_;
  struct PendingSave {
    gui_session::SessionId session = 0;
    std::filesystem::path path;
    bool thenStart = false;
    std::future<std::expected<void, std::string>> result;
  };
  std::vector<PendingSave> pendingSaves_;
  struct PendingViewSave {
    gui_session::SessionId session = 0;
    bool thenStart = false;
    std::future<InPlaceSaveResult> result;
    std::optional<gui_session_file::DecoderUpdate> decoders;
    std::vector<std::pair<std::string, std::string>> decodersAtStart;
    std::vector<std::string> presentationAtStart;
  };
  std::vector<PendingViewSave> pendingViewSaves_;
  struct PendingOpen {
    std::filesystem::path path;
    bool restore = false;
    std::future<std::expected<std::vector<gui_session_file::LoadedPanel>, std::string>> result;
  };
  std::vector<PendingOpen> pendingOpens_;
  std::vector<std::string> restoreRemaining_;
  bool restoreRunning_ = false;
  bool restoreInFlight_ = false;
  std::optional<std::string> restoreSelect_;
  std::optional<gui_session::SessionId> focusSession_;
  int focusDelayFrames_ = 0;
  std::optional<std::string> fileError_;
  struct ReplaceQuestion {
    gui_session::SessionId session = 0;
    std::filesystem::path path;
    bool thenStart = false;
  };
  std::optional<ReplaceQuestion> replaceQuestion_;

  gui_capture::CloseFlow closeFlow_;
  gui_capture::CloseFlow::Action lastCloseAction_;
  bool closeConfirmed_ = false;

  std::optional<Wizard> wizard_;
  bool wizardToOpen_ = false;
  int logMinimumLevel_ = 1;
  bool logAutoScroll_ = true;
  bool logJustShown_ = false;
  bool logGrew_ = false;
  bool logGrewSinceFrame_ = false;
  bool reduceIdleFrameRate_ = false;

  std::unique_ptr<gui_devices::DeviceWorker> deviceWorker_;
  gui_devices::PipeDeviceHost* pipeHost_ = nullptr;
  gui_devices::ListAgainMemory listAgainMemory_;
};

}
