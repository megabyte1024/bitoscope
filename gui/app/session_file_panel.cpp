/*
 * SPDX-License-Identifier: MIT
 */

#include <imgui.h>

#include <decoder_chain_config.h>
#include <decoder_presentation.h>
#include <decoder_save.h>
#include <decoder_versions.h>
#include <default_channel_colors.h>
#include <display_format.h>
#include <plugin_decoder.h>
#include <popup.h>
#include <recent_files.h>
#include <save_command.h>

#include <filesystem>
#include <format>
#include <future>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "gui_app.h"
#include "platform_services.h"
#include "session_waveform.h"
#include "theme.h"
#include "tr.h"

namespace bitoscope::gui {

namespace {

namespace gc = gui_capture;
namespace gsf = gui_session_file;
namespace wv = waveform_view;

std::string utf8(const std::filesystem::path& path) {
  const std::u8string text = path.u8string();
  return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}

std::filesystem::path pathOf(std::string_view utf8Text) {
  return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(utf8Text.data()), utf8Text.size()));
}

bool fileExists(std::string_view utf8Text) {
  std::error_code ec;
  return std::filesystem::is_regular_file(pathOf(utf8Text), ec);
}

void tooltipAnyway(const char* text) {
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled)) {
    ImGui::SetTooltip("%s", text);
  }
}

}

std::string GuiApp::saveBlockedText(gc::PanelCapture::SaveBlocked why) {
  switch (why) {
  case gc::PanelCapture::SaveBlocked::NoCapture:
    return tr("No capture to save");
  case gc::PanelCapture::SaveBlocked::Running:
    return tr("Capture is running: it can be saved when it ends");
  case gc::PanelCapture::SaveBlocked::Saving:
    return tr("Saving...");
  }
  return {};
}

void GuiApp::requestSave(gui_session::SessionId id, bool thenStart, gsf::SaveCommand command) {
  const auto ui = sessionUi_.find(id);
  if (ui == sessionUi_.end()) {
    return;
  }
  if (const auto blocked = ui->second.capture.saveBlocked()) {
    status(saveBlockedText(*blocked));
    return;
  }
  const auto& known = ui->second.capture.knownFile();
  if (!gsf::saveAsksWhere(command, known)) {
    saveTo(id, pathOf(known->path), thenStart);
    return;
  }
  fileRequest_ = FileRequest{FileRequest::Kind::Save, id, thenStart};
}

void GuiApp::requestOpen() { fileRequest_ = FileRequest{FileRequest::Kind::Open, 0, false}; }

void GuiApp::runFileRequest() {
  if (!fileRequest_) {
    return;
  }
  const FileRequest request = *fileRequest_;
  fileRequest_.reset();
  PlatformServices::FileDialog dialog;
  dialog.fileTypeName = tr("bitoscope session");
  dialog.allFilesName = tr("All files");
  dialog.folder = pathOf(prefs_.lastFileFolder);

  if (request.kind == FileRequest::Kind::Open) {
    dialog.title = tr("Open session");
    const auto chosen = platform_.askOpenPath(dialog);
    if (!chosen) {
      logger_.logf(log::LogLevel::error, "file", "Open dialog could not be shown: {}", chosen.error());
      fileError_ = trf("Open dialog could not be shown: {}", chosen.error());
      return;
    }
    if (!*chosen) {
      return;
    }
    openFile(**chosen);
    return;
  }

  const gui_session::Session* session = sessions_.find(request.session);
  const auto ui = sessionUi_.find(request.session);
  if (session == nullptr || ui == sessionUi_.end()) {
    return;
  }
  if (const auto blocked = ui->second.capture.saveBlocked()) {
    status(saveBlockedText(*blocked));
    return;
  }
  dialog.title = trf("Save {}", session->name);
  gsf::SaveDialogStart start =
    gsf::saveDialogStart(ui->second.capture.knownFile(), dialog.folder,
                         gsf::suggestedFileName(session->name, gsf::localCivilTime(ui->second.capturedAt)));
  dialog.folder = std::move(start.folder);
  dialog.fileName = std::move(start.fileName);
  const auto chosen = platform_.askSavePath(dialog);
  if (!chosen) {
    logger_.logf(log::LogLevel::error, "file", "Save dialog could not be shown: {}", chosen.error());
    fileError_ = trf("Save dialog could not be shown: {}", chosen.error());
    return;
  }
  if (!*chosen) {
    logger_.logf(log::LogLevel::info, "file", "{}: Save cancelled; capture is not saved", session->name);
    return;
  }
  if (platform_.saveNeedsReplaceQuestion(**chosen)) {
    replaceQuestion_ = ReplaceQuestion{request.session, **chosen, request.thenStart};
    return;
  }
  saveTo(request.session, **chosen, request.thenStart);
}

void GuiApp::saveTo(gui_session::SessionId id, const std::filesystem::path& path, bool thenStart) {
  const auto ui = sessionUi_.find(id);
  if (ui == sessionUi_.end()) {
    return;
  }
  // WHY: Saving onto the known file rewrites only its view: a full rewrite would shrink a multi-device file to one.
  const auto& known = ui->second.capture.knownFile();
  if (gsf::saveWriteFor(known, path, gsf::knownFileOnDisk(known)) == gsf::SaveWrite::ViewOnly) {
    startViewSave(id, *known, thenStart);
    return;
  }
  startSave(id, path, thenStart);
}

void GuiApp::startSave(gui_session::SessionId id, const std::filesystem::path& path, bool thenStart) {
  const gui_session::Session* session = sessions_.find(id);
  SessionUi& ui = sessionUi_[id];
  const auto& kept = ui.capture.kept();
  if (session == nullptr || !kept) {
    return;
  }
  gsf::SaveView view;
  view.deviceChannels = ui.keptDeviceChannels;
  for (const std::uint32_t d : ui.keptDeviceChannels) {
    view.channelNames.push_back(d < ui.channelNames.size() ? ui.channelNames[d] : std::string());
    view.channelUserNames.push_back(d < ui.channelUserNames.size() ? ui.channelUserNames[d] : std::string());
    view.channelColors.push_back(d < ui.channelColors.size() ? ui.channelColors[d] : wv::ColorChoice::palette(d));
  }
  if (ui.waveform) {
    view.markers = ui.waveform->markers;
    view.cursors = ui.waveform->cursors;
    view.showCursors = ui.waveform->showCursors;
  }
  view.laneOrder = ui.laneOrder;
  view.decoderIds = gui_decode::decoderLaneKeys(ui.decoders);
  view.deviceProperties = ui.keptDeviceProperties;
  view.decoderSetup = gui_decode::decoderSetupOf(ui.decoders, decoderRegistry());
  // WHY: A decoder skipped on load is written back at its original slot, so a re-save does not lose it.
  {
    auto save = gsf::decoderSetupForSave(ui.loadedDecoderSetup, ui.loadedDecoderSkipped, view.decoderSetup,
                                         ui.loadedDecoderPresentation,
                                         gui_decode::decoderPresentationOf(ui.decoders, decoderRegistry()),
                                         gui_decode::unchangedFileEntries(ui.decoders, decoderRegistry()),
                                         ui.loadedDecoderVersions,
                                         gui_decode::savedDecoderVersionsOf(ui.decoders, decoderRegistry()));
    view.decoderSetup = std::move(save.setup);
    view.decoderPresentation = std::move(save.presentation);
    view.decoderVersions = std::move(save.versions);
  }
  auto metadata = gsf::captureMetadataFor(*kept, view);
  if (!metadata) {
    logger_.logf(log::LogLevel::error, "file", "{}: capture cannot be saved: {}", session->name, metadata.error());
    fileError_ = trf("Capture cannot be saved: {}", metadata.error());
    return;
  }
  if (!ui.capture.beginSave()) {
    return;
  }
  gc::CaptureOutcome capture = *kept;
  logger_.logf(log::LogLevel::info, "file", "{}: saving {} samples to {}", session->name,
               gui_format::withThousands(capture.storedSamples), utf8(path));
  status(trf("Saving {}...", utf8(path.filename())));
  PendingSave pending;
  pending.session = id;
  pending.path = path;
  pending.thenStart = thenStart;
  pending.result = std::async(std::launch::async, [capture = std::move(capture), metadata = std::move(*metadata), path] {
    return gsf::writeSessionFile(path, capture, metadata);
  });
  pendingSaves_.push_back(std::move(pending));
}

void GuiApp::saveFinished(gui_session::SessionId id, const std::filesystem::path& path, bool thenStart,
                          const std::expected<void, std::string>& result) {
  const gui_session::Session* session = sessions_.find(id);
  const std::string name = session != nullptr ? session->name : std::to_string(id);
  const auto ui = sessionUi_.find(id);
  if (ui != sessionUi_.end()) {
    ui->second.capture.fullSaveFinished(gc::PanelCapture::KnownFile{utf8(path), 0}, result);
  }
  closeFlow_.saveFinished(id, result.has_value());
  if (!result) {
    logger_.logf(log::LogLevel::error, "file", "{}: capture was NOT saved to {}: {}", name, utf8(path),
                 result.error());
    fileError_ = trf("Capture was not saved: {}", result.error());
    status(tr("Not saved"));
    return;
  }
  logger_.logf(log::LogLevel::info, "file", "{}: saved to {}", name, utf8(path));
  sessions_.savedTo(id, utf8(path));
  status(trf("Saved to {}", utf8(path.filename())));
  rememberFile(path);
  if (thenStart && ui != sessionUi_.end() && ui->second.startFlow.step() == gc::StartFlow::Step::AskUnsaved) {
    ui->second.startFlow.answerUnsaved(gc::UnsavedChoice::Save, false);
    advanceStartFlow(id, ui->second);
  }
}

void GuiApp::startViewSave(gui_session::SessionId id, const gc::PanelCapture::KnownFile& file, bool thenStart) {
  const gui_session::Session* session = sessions_.find(id);
  SessionUi& ui = sessionUi_[id];
  if (session == nullptr || !ui.capture.kept()) {
    return;
  }
  gsf::SaveView view;
  view.deviceChannels = ui.keptDeviceChannels;
  for (const std::uint32_t d : ui.keptDeviceChannels) {
    view.channelColors.push_back(d < ui.channelColors.size() ? ui.channelColors[d] : wv::ColorChoice::palette(d));
    view.channelUserNames.push_back(d < ui.channelUserNames.size() ? ui.channelUserNames[d] : std::string());
  }
  if (ui.waveform) {
    view.markers = ui.waveform->markers;
    view.cursors = ui.waveform->cursors;
    view.showCursors = ui.waveform->showCursors;
  }
  view.laneOrder = ui.laneOrder;
  view.decoderIds = gui_decode::decoderLaneKeys(ui.decoders);
  std::optional<gsf::DecoderUpdate> decoders;
  std::vector<std::pair<std::string, std::string>> decodersAtStart;
  std::vector<std::string> presentationAtStart;
  if (ui.capture.decodersUnsaved()) {
    const std::vector<std::optional<std::size_t>> unchanged =
      gui_decode::unchangedFileEntries(ui.decoders, decoderRegistry());
    std::vector<gsf::DecoderToSave> current;
    for (std::size_t i = 0; i < ui.decoders.size(); ++i) {
      const gui_decode::DecoderConfig& config = ui.decoders[i];
      auto decoder = decoderRegistry().create(config.decoderName());
      if (!decoder) {
        continue;
      }
      current.push_back({config.decoderName(), config.toSpecString(**decoder, ui.decoders), unchanged[i],
                         gui_decode::presentationText(config), decoder_host::savedVersionsOf(**decoder)});
    }
    decoders = gsf::decoderSetupForUpdate(ui.loadedDecoderSetup, ui.loadedDecoderSkipped, current,
                                          ui.loadedDecoderPresentation, ui.loadedDecoderVersions);
    decodersAtStart = gui_decode::decoderSetupOf(ui.decoders, decoderRegistry());
    presentationAtStart = gui_decode::decoderPresentationOf(ui.decoders, decoderRegistry());
  }
  if (!ui.capture.beginSave()) {
    return;
  }
  logger_.logf(log::LogLevel::info, "file", "{}: saving {} to {}", session->name,
               decoders ? "view and decoders" : "view", file.path);
  status(trf("Saving {}...", utf8(pathOf(file.path).filename())));
  PendingViewSave pending;
  pending.session = id;
  pending.thenStart = thenStart;
  pending.decoders = decoders;
  pending.decodersAtStart = std::move(decodersAtStart);
  pending.presentationAtStart = std::move(presentationAtStart);
  const std::string path = file.path;
  const std::uint32_t deviceIndex = file.deviceIndex;
  pending.result = std::async(std::launch::async, [path, deviceIndex, view = std::move(view),
                                                   decoders = std::move(decoders)] {
    InPlaceSaveResult done{gsf::updateSessionView(pathOf(path), deviceIndex, view), std::nullopt};
    if (done.view && decoders) {
      done.decoders = gsf::updateSessionDecoders(pathOf(path), deviceIndex, *decoders);
    }
    return done;
  });
  pendingViewSaves_.push_back(std::move(pending));
}

void GuiApp::viewSaveFinished(gui_session::SessionId id, bool thenStart, const InPlaceSaveResult& result,
                              const std::optional<gsf::DecoderUpdate>& decoders,
                              const std::vector<std::pair<std::string, std::string>>& decodersAtStart,
                              const std::vector<std::string>& presentationAtStart) {
  const gui_session::Session* session = sessions_.find(id);
  const std::string name = session != nullptr ? session->name : std::to_string(id);
  const auto ui = sessionUi_.find(id);
  const bool decodersFailed = result.decoders && !*result.decoders;
  closeFlow_.saveFinished(id, result.view.has_value() && !decodersFailed);
  if (ui != sessionUi_.end()) {
    ui->second.capture.viewSaveFinished(!result.view    ? result.view
                                        : decodersFailed ? *result.decoders
                                                         : std::expected<void, std::string>{});
  }
  if (!result.view) {
    logger_.logf(log::LogLevel::error, "file", "{}: view was NOT saved: {}", name, result.view.error());
    fileError_ = trf("View could not be saved: {}", result.view.error());
    status(tr("Not saved"));
    return;
  }
  if (decodersFailed) {
    logger_.logf(log::LogLevel::error, "file", "{}: decoders were NOT saved: {}", name, result.decoders->error());
    fileError_ = trf("Decoders could not be saved: {}", result.decoders->error());
    status(tr("Not saved"));
    return;
  }
  if (result.decoders && decoders && ui != sessionUi_.end()) {
    SessionUi& saved = ui->second;
    if (gui_decode::decoderSetupOf(saved.decoders, decoderRegistry()) == decodersAtStart &&
        gui_decode::decoderPresentationOf(saved.decoders, decoderRegistry()) == presentationAtStart) {
      saved.loadedDecoderSetup = decoders->setup;
      saved.loadedDecoderSkipped = decoders->skippedAt;
      saved.loadedDecoderPresentation = decoders->presentation;
      saved.loadedDecoderVersions = decoders->versions;
      const std::vector<std::optional<std::size_t>> entries(decoders->currentAt.begin(), decoders->currentAt.end());
      gui_decode::recordLoadedSetup(saved.decoders, entries, decoderRegistry());
    } else {
      saved.capture.markDecodersEdited();
    }
  }
  logger_.logf(log::LogLevel::info, "file", "{}: {} saved", name, result.decoders ? "view and decoders" : "view");
  status(tr("Saved"));
  if (thenStart && ui != sessionUi_.end() && ui->second.startFlow.step() == gc::StartFlow::Step::AskUnsaved) {
    ui->second.startFlow.answerUnsaved(gc::UnsavedChoice::Save, false);
    advanceStartFlow(id, ui->second);
  }
}

void GuiApp::openFile(const std::filesystem::path& path, bool restore) {
  if (restore) {
    logger_.logf(log::LogLevel::info, "file", "Reopening {} from previous session", utf8(path));
  } else {
    logger_.logf(log::LogLevel::info, "file", "Opening {}", utf8(path));
  }
  status(trf("Opening {}...", utf8(path.filename())));
  PendingOpen pending;
  pending.path = path;
  pending.restore = restore;
  pending.result = std::async(std::launch::async, [path] { return gsf::openSessionFile(path); });
  pendingOpens_.push_back(std::move(pending));
}

void GuiApp::opened(const std::filesystem::path& path,
                    std::expected<std::vector<gsf::LoadedPanel>, std::string> result, bool restore) {
  if (!result) {
    if (restore) {
      logger_.logf(log::LogLevel::warning, "file", "Previous session file {} could not be reopened: {}", utf8(path),
                   result.error());
      return;
    }
    logger_.logf(log::LogLevel::error, "file", "{} could not be opened: {}", utf8(path), result.error());
    fileError_ = trf("{} could not be opened: {}", utf8(path.filename()), result.error());
    status(tr("Not opened"));
    return;
  }
  if (!restore) {
    noteRecentFile(path);
  }
  const std::string stem = utf8(path.stem());
  const std::size_t count = result->size();
  for (std::size_t k = 0; k < count; ++k) {
    gsf::LoadedPanel& panel = (*result)[k];
    const std::string base = count == 1 ? stem : std::format("{} #{}", stem, k + 1);
    const gui_session::SessionId id = sessions_.add(
      [&base](std::uint64_t n) { return n == 1 ? base : std::format("{} ({})", base, n); });
    sessions_.setOpenedFrom(id, utf8(path));
    SessionUi& ui = sessionUi_[id];
    ui.device = utf8(path.filename());
    ui.openedFrom = utf8(path);
    ui.openedInfo = trf("Opened from {}", utf8(path));
    if (!panel.device.empty()) {
      ui.openedInfo += "\n" + trf("Device: {}", panel.device);
    }
    ui.openedInfo += "\n" + trf("Sample rate: {}", gui_format::formatFrequency(panel.sampleRateMilliHz));
    ui.openedInfo += "\n" + trfn("{} channel", "{} channels", static_cast<unsigned long>(panel.deviceChannels.size()),
                                 panel.deviceChannels.size());
    if (panel.outcome.triggerSampleIndex) {
      ui.openedInfo += "\n" + std::string(tr("Triggered capture"));
    }
    ui.keptDeviceChannels = panel.deviceChannels;
    ui.fileLanesShown.assign(panel.deviceChannels.size(), true);
    ui.channelNames = std::move(panel.channelNames);
    ui.channelUserNames = std::move(panel.channelUserNames);
    ui.channelDefaultColors = gc::defaultChannelColors(static_cast<std::uint32_t>(panel.channelColors.size()), {});
    ui.channelColors = std::move(panel.channelColors);
    ui.keptDeviceProperties = std::move(panel.deviceProperties);
    ui.waveform = std::make_shared<SessionWaveform>();
    ui.waveform->view = id;
    ui.waveform->markers = std::move(panel.markers);
    ui.waveform->cursors = panel.cursors;
    ui.waveform->showCursors = panel.showCursors;
    const std::string name = sessions_.find(id)->name;
    for (const std::string& warning : panel.warnings) {
      logger_.logf(log::LogLevel::warning, "file", "{}: {}", name, warning);
    }
    logger_.logf(log::LogLevel::info, "file", "{}: opened from {}: {} of {} samples{}", name, utf8(path),
                 gui_format::withThousands(panel.outcome.storedSamples),
                 gui_format::withThousands(panel.outcome.requestedSamples),
                 panel.outcome.isShort() ? " (short)" : "");
    // WHY: Device index INSIDE the file: a view-only save must name the same device back.
    ui.capture.loaded(std::move(panel.outcome), gc::PanelCapture::KnownFile{utf8(path), static_cast<std::uint32_t>(k)});
    ui.loadedDecoderSetup = panel.decoderSetup;
    ui.loadedDecoderSkipped.clear();
    ui.loadedDecoderPresentation = panel.decoderPresentation;
    ui.loadedDecoderVersions = panel.decoderVersions;
    std::vector<std::optional<std::size_t>> entries;
    const std::vector<gui_decode::CapturedChannel> fileChannels = capturedChannelsFor(ui);
    for (std::size_t n = 0; n < panel.decoderSetup.size(); ++n) {
      const auto& [decoderName, specRest] = panel.decoderSetup[n];
      // WHY: Already rebuilt configs go along as `earlier`, so two unnamed decoders of one name get different ids.
      auto config = gui_decode::decoderConfigFromSetup(decoderRegistry(), decoderName, specRest, ui.decoders,
                                                       fileChannels);
      if (config) {
        ui.decoders.push_back(std::move(*config));
        entries.push_back(n);
      } else {
        ui.loadedDecoderSkipped.push_back(n);
        logger_.logf(log::LogLevel::warning, "file", "{}: decoder '{}' from file was skipped: {}", name,
                     decoderName, config.error());
      }
    }
    gui_decode::recordLoadedSetup(ui.decoders, entries, decoderRegistry());
    gui_decode::applyLoadedPresentations(ui.decoders, entries, panel.decoderPresentation, decoderRegistry());
    {
      const std::vector<std::size_t> decoderOrder = gui_decode::laneOrder(ui.decoders);
      // WHY: Lane keys naming no rebuilt decoder are DROPPED: else later lanes would read shifted by one.
      std::optional<wv::LaneOrder> saved;
      if (panel.laneOrderText) {
        auto parsed = wv::parseWithDecoderKeys(*panel.laneOrderText, gui_decode::decoderLaneKeys(ui.decoders));
        if (parsed) {
          for (const std::string& key : parsed->droppedKeys) {
            logger_.logf(log::LogLevel::warning, "file", "{}: saved lane order names decoder '{}', which this "
                         "file did not bring back: its lane takes its default place", name, key);
          }
          saved = std::move(parsed->order);
        } else {
          logger_.logf(log::LogLevel::warning, "file",
                       "{}: saved lane order '{}' is not one this version reads ({}); default lane order is used",
                       name, *panel.laneOrderText, parsed.error());
        }
      }
      ui.laneOrder = saved ? std::move(*saved) : wv::defaultLaneOrder(ui.keptDeviceChannels, decoderOrder);
      ui.laneOrder.sync(ui.keptDeviceChannels, ui.decoders.size());
    }
    runDecodersForSession(id, ui);
  }
  status(trf("Opened {}", utf8(path.filename())));
  if (!restore) {
    savePreferencesNow();
  }
}

void GuiApp::noteRecentFile(const std::filesystem::path& path) {
  prefs_.recentFiles = gui_prefs::withRecentFile(std::move(prefs_.recentFiles), utf8(path));
  prefs_.lastFileFolder = utf8(path.parent_path());
}

void GuiApp::rememberFile(const std::filesystem::path& path) {
  noteRecentFile(path);
  savePreferencesNow();
}

std::string GuiApp::sessionFileOf(gui_session::SessionId id) const {
  const auto ui = sessionUi_.find(id);
  if (ui == sessionUi_.end() || !ui->second.capture.knownFile()) {
    return {};
  }
  return ui->second.capture.knownFile()->path;
}

std::vector<std::string> GuiApp::filesBeingOpened() const {
  std::vector<std::string> files;
  for (const PendingOpen& pending : pendingOpens_) {
    if (!pending.restore) {
      files.push_back(utf8(pending.path));
    }
  }
  return files;
}

void GuiApp::continueRestore() {
  while (restoreRunning_ && !restoreInFlight_ && !restoreRemaining_.empty()) {
    const std::string next = restoreRemaining_.front();
    if (!fileExists(next)) {
      logger_.logf(log::LogLevel::warning, "file", "Previous session file {} no longer exists: skipped", next);
      restoreRemaining_.erase(restoreRemaining_.begin());
      continue;
    }
    restoreInFlight_ = true;
    constexpr bool restoreSession = true;
    openFile(pathOf(next), restoreSession);
  }
  if (restoreRunning_ && !restoreInFlight_ && restoreRemaining_.empty()) {
    finishRestore();
  }
}

void GuiApp::finishRestore() {
  restoreRunning_ = false;
  if (restoreSelect_) {
    for (const gui_session::Session& session : sessions_.sessions()) {
      const std::string file = sessionFileOf(session.id);
      if (!file.empty() && gui_prefs::sameFilePath(file, *restoreSelect_)) {
        sessions_.setActive(session.id);
        focusSession_ = session.id;
        focusDelayFrames_ = 3;
        break;
      }
    }
    restoreSelect_.reset();
  }
  savePreferencesNow();
}

bool GuiApp::pollFileWork() {
  bool anyEnded = false;
  for (auto it = pendingSaves_.begin(); it != pendingSaves_.end();) {
    if (it->result.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
      ++it;
      continue;
    }
    anyEnded = true;
    PendingSave done = std::move(*it);
    it = pendingSaves_.erase(it);
    saveFinished(done.session, done.path, done.thenStart, done.result.get());
  }
  for (auto it = pendingViewSaves_.begin(); it != pendingViewSaves_.end();) {
    if (it->result.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
      ++it;
      continue;
    }
    anyEnded = true;
    PendingViewSave done = std::move(*it);
    it = pendingViewSaves_.erase(it);
    viewSaveFinished(done.session, done.thenStart, done.result.get(), done.decoders, done.decodersAtStart,
                     done.presentationAtStart);
  }
  bool restoreStepDone = false;
  for (auto it = pendingOpens_.begin(); it != pendingOpens_.end();) {
    if (it->result.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
      ++it;
      continue;
    }
    anyEnded = true;
    PendingOpen done = std::move(*it);
    it = pendingOpens_.erase(it);
    opened(done.path, done.result.get(), done.restore);
    if (done.restore) {
      restoreInFlight_ = false;
      restoreRemaining_.erase(restoreRemaining_.begin());
      restoreStepDone = true;
    }
  }
  if (restoreStepDone) {
    continueRestore();  // WHY: continueRestore() runs after the loop: it may add to pendingOpens_.
  }
  return anyEnded;
}

void GuiApp::finishFileWork() {
  for (PendingSave& pending : pendingSaves_) {
    const auto result = pending.result.get();
    if (result) {
      logger_.logf(log::LogLevel::info, "file", "Saved to {} while closing", utf8(pending.path));
      prefs_.recentFiles = gui_prefs::withRecentFile(std::move(prefs_.recentFiles), utf8(pending.path));
      prefs_.lastFileFolder = utf8(pending.path.parent_path());
    } else {
      logger_.logf(log::LogLevel::error, "file", "NOT saved to {}: {}", utf8(pending.path), result.error());
    }
  }
  pendingSaves_.clear();
  for (PendingViewSave& pending : pendingViewSaves_) {
    const InPlaceSaveResult result = pending.result.get();
    if (!result.view) {
      logger_.logf(log::LogLevel::error, "file", "NOT saved (view): {}", result.view.error());
    } else if (result.decoders && !*result.decoders) {
      logger_.logf(log::LogLevel::error, "file", "NOT saved (decoders): {}", result.decoders->error());
    }
  }
  pendingViewSaves_.clear();
  for (PendingOpen& pending : pendingOpens_) {
    pending.result.wait();
  }
}

void GuiApp::drawOpenRecentMenu() {
  const std::vector<gui_prefs::RecentFileEntry> entries =
    gui_prefs::recentFileEntries(prefs_.recentFiles, [](std::string_view path) { return fileExists(path); });
  if (entries.empty()) {
    ImGui::MenuItem(tr("No recent files"), nullptr, false, false);
  }
  std::optional<std::string> chosen;
  for (std::size_t i = 0; i < entries.size(); ++i) {
    const gui_prefs::RecentFileEntry& entry = entries[i];
    ImGui::PushID(static_cast<int>(i));
    if (!entry.exists) {
      ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    }
    if (ImGui::MenuItem(entry.name.c_str())) {
      chosen = entry.path;
    }
    if (!entry.exists) {
      ImGui::PopStyleColor();
      tooltipAnyway(trf("{}\nNot found: choose it to remove it from this list", entry.path).c_str());
    } else {
      tooltipAnyway(entry.path.c_str());
    }
    ImGui::PopID();
  }
  ImGui::Separator();
  if (ImGui::MenuItem(tr("Clear list"), nullptr, false, !entries.empty())) {
    prefs_.recentFiles.clear();
    logger_.log(log::LogLevel::info, "file", "Recent files list cleared");
    savePreferencesNow();
  }
  if (chosen) {
    const gui_prefs::RecentChoice choice =
      gui_prefs::chooseRecentFile(prefs_.recentFiles, *chosen, [](std::string_view path) { return fileExists(path); });
    if (choice.action == gui_prefs::RecentChoice::Action::Open) {
      openFile(pathOf(*chosen));
    } else {
      logger_.log(log::LogLevel::warning, "file", choice.logLine);
      status(trf("Not found, removed from recent files: {}", *chosen));
      savePreferencesNow();
    }
  }
}

void GuiApp::drawReplaceModal() {
  const std::string id = withId(tr("Replace"), "file/replace");
  if (replaceQuestion_ && !ImGui::IsPopupOpen(id.c_str())) {
    ImGui::OpenPopup(id.c_str());
  }
  ImGui::SetNextWindowSize(ImVec2(ImGui::GetFontSize() * 28.0f, 0));
  if (!ImGui::BeginPopupModal(id.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    return;
  }
  if (replaceQuestion_) {
    ImGui::TextWrapped("%s", trf("Replace {}?", utf8(replaceQuestion_->path.filename())).c_str());
  }
  ImGui::Separator();
  const bool replace = ImGui::Button(tr("Replace"));
  ImGui::SameLine();
  // WHY: No Enter shortcut: replacing a file is not the default answer.
  const bool cancel = ImGui::Button(tr("Cancel")) || ImGui::IsKeyPressed(ImGuiKey_Escape);
  if ((replace || cancel) && replaceQuestion_) {
    const ReplaceQuestion question = *replaceQuestion_;
    replaceQuestion_.reset();
    ImGui::CloseCurrentPopup();
    if (replace) {
      saveTo(question.session, question.path, question.thenStart);
    } else {
      const gui_session::Session* session = sessions_.find(question.session);
      logger_.logf(log::LogLevel::info, "file", "{}: Save cancelled; {} is not replaced",
                   session != nullptr ? session->name : std::to_string(question.session), utf8(question.path));
    }
  }
  ImGui::EndPopup();
}

void GuiApp::drawFileErrorModal() {
  const std::string id = withId(tr("File"), "file/error");
  if (fileError_ && !ImGui::IsPopupOpen(id.c_str())) {
    ImGui::OpenPopup(id.c_str());
  }
  ImGui::SetNextWindowSize(ImVec2(ImGui::GetFontSize() * 28.0f, 0));
  if (ImGui::BeginPopupModal(id.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    if (fileError_) {
      ImGui::TextWrapped("%s", fileError_->c_str());
    }
    ImGui::Separator();
    if (gui_popup::rightAlignedButton(tr("OK")) || ImGui::IsKeyPressed(ImGuiKey_Escape) ||
        ImGui::IsKeyPressed(ImGuiKey_Enter)) {
      fileError_.reset();
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
}

}
