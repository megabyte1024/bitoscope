/*
 * SPDX-License-Identifier: MIT
 */

#include <imgui.h>

#include <acquisition.h>
#include <capture_outcome.h>
#include <channel_selection.h>
#include <color_choice.h>
#include <decoder_save.h>
#include <default_channel_colors.h>
#include <display_format.h>
#include <fixed_time.h>
#include <length_text.h>
#include <pipe_device_host.h>
#include <text_field.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <format>
#include <map>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "gui_app.h"
#include "session_waveform.h"
#include "theme.h"
#include "tr.h"

namespace bitoscope::gui {

namespace {

namespace gc = gui_capture;
namespace wv = waveform_view;

void tooltipAnyway(const char* text) {
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled)) {
    ImGui::SetTooltip("%s", text);
  }
}

std::string durationText(std::uint64_t samples, std::uint64_t rate) {
  if (rate == 0) {
    return {};
  }
  return gui_format::formatTimePs(wv::sampleTimePicoseconds(static_cast<std::int64_t>(samples), rate));
}

gc::UnsavedPolicy policyOf(gui_prefs::UnsavedCapturePolicy policy) {
  return policy == gui_prefs::UnsavedCapturePolicy::DontSave ? gc::UnsavedPolicy::DontSave : gc::UnsavedPolicy::Ask;
}

std::uint32_t hardwareThreads() { return std::thread::hardware_concurrency(); }

}

void GuiApp::syncCaptureSettings(const gui_session::Session& session, SessionUi& ui) {
  if (!session.device.has_value()) {
    return;
  }
  const ipc::DeviceInfo* opened = router_.openedDevice(session.device->driver, session.device->id);
  if (opened == nullptr) {
    return;
  }
  const std::string key = session.device->driver + "/" + session.device->id;
  if (key == ui.capabilitiesKey) {
    return;
  }
  ui.capabilitiesKey = key;
  auto capabilities = gc::capabilitiesOf(*opened);
  if (!capabilities) {
    ui.capabilities.reset();
    ui.capabilitiesProblem = capabilities.error();
    logger_.logf(log::LogLevel::warning, "capture", "{}: {}", session.name, capabilities.error());
    return;
  }
  ui.capabilities = *capabilities;
  ui.capabilitiesProblem.clear();
  ui.settings = ui.settings ? gc::fitToDevice(*ui.settings, *capabilities) : gc::defaultSettings(*capabilities);
  ui.generatorSwitchOff.clear();
  if (pipeHost_ != nullptr) {
    gui_properties::OpenedDeviceProperties properties =
      gui_properties::openDeviceProperties(*opened, pipeHost_->pluginHost(), key);
    ui.deviceProperties.emplace(std::move(properties.model));
    ui.generatorSwitchOff = std::move(properties.generators);
  } else {
    ui.deviceProperties.emplace(*opened);
  }
  for (const gui_properties::GeneratorSwitchOff& generator : ui.generatorSwitchOff) {
    if (!generator.unreadable.empty()) {
      logger_.logf(log::LogLevel::warning, "capture",
                   "{}: generator outputs could not be switched off, state unknown: {}", session.name,
                   generator.unreadable);
    }
    for (const gui_properties::OutputLeftUnknown& output : generator.leftUnknown) {
      logger_.logf(log::LogLevel::warning, "capture",
                   "{}: generator output {} could not be switched off, state unknown: {}", session.name,
                   output.output, output.reason);
    }
    if (generator.allOff()) {
      logger_.logf(log::LogLevel::info, "capture", "{}: every generator output switched off ({})", session.name,
                   generator.outputs);
    }
  }
  ui.propertyTextBuffers.clear();
  constexpr bool switchOff = false;
  syncGeneratorOutputs(ui, switchOff);
  // WHY: Default channel colors are stored apart from user colors: Reset to default must be able to restore them.
  const std::uint32_t channels = capabilities->num_channels;
  const std::size_t before = ui.channelColors.size();
  ui.channelDefaultColors = gc::defaultChannelColors(channels, capabilities->channel_presets);
  ui.channelColors.resize(channels);
  for (std::uint32_t c = static_cast<std::uint32_t>(before); c < channels; ++c) {
    ui.channelColors[c] = ui.channelDefaultColors[c];
  }
  ui.channelNames.assign(channels, std::string());
  for (const ipc::la::ChannelPreset& preset : capabilities->channel_presets) {
    if (preset.channel >= channels) {
      continue;
    }
    ui.channelNames[preset.channel] = preset.name;
  }
  logger_.logf(log::LogLevel::info, "capture", "{}: {} channels, rates {}",
               session.name, channels,
               capabilities->supported_sample_rates_milli_hz.empty()
                 ? std::string("chosen by driver")
                 : gui_format::formatFrequency(capabilities->supported_sample_rates_milli_hz.front()) + " to " +
                     gui_format::formatFrequency(*std::max_element(
                       capabilities->supported_sample_rates_milli_hz.begin(),
                       capabilities->supported_sample_rates_milli_hz.end())));
}

std::string GuiApp::settingsProblemText(const gui_capture::SettingsProblem& problem) const {
  using Kind = gc::SettingsProblem::Kind;
  switch (problem.kind) {
  case Kind::NoChannel:
    return tr("No channel is selected: select at least one.");
  case Kind::TooManyChannels:
    return trf("Channel {} does not exist on this device.", problem.channel);
  case Kind::NoRate:
    return tr("No sample rate is chosen.");
  case Kind::RateNotOffered:
    return trf("This device does not offer sample rate {}.", gui_format::formatFrequency(problem.rate));
  case Kind::RateAboveCeiling:
    return trfn("With {} channel this device samples at most at {}; {} is too fast. Choose lower rate or fewer "
                "channels.",
                "With {} channels this device samples at most at {}; {} is too fast. Choose lower rate or fewer "
                "channels.",
                problem.channels, problem.channels, gui_format::formatFrequency(problem.ceiling),
                gui_format::formatFrequency(problem.rate));
  case Kind::NoLength:
    return tr("Capture length is zero.");
  case Kind::TriggerNotCaptured:
    return trf("Trigger uses channel {}, which is not captured: select it, or clear its trigger.",
               problem.channel);
  case Kind::BadPreTrigger:
    return tr("Pre-trigger share is not between 0 and 100 %.");
  }
  return {};
}

void GuiApp::sendPropertyNow(const SessionUi& ui, const std::string& facetName,
                             const gui_properties::PropertyValue& value) {
  // WHY: Send only the single edited value: valuesToSend() would re-send every other already-set property too.
  sendFacetPropertiesNow(ui, facetName, std::vector{value});
}

void GuiApp::sendFacetPropertiesNow(const SessionUi& ui, const std::string& facetName,
                                    const std::vector<gui_properties::PropertyValue>& values) {
  if (pipeHost_ == nullptr || values.empty()) {
    return;
  }
  auto sent = pipeHost_->pluginHost().setProperties(ui.capabilitiesKey, facetName, values);
  if (!sent) {
    status(trf("Could not change '{}': {}", facetName, sent.error().message));
  }
}

void GuiApp::syncGeneratorOutputs(SessionUi& ui, bool switchOff) {
  if (!ui.deviceProperties) {
    return;
  }
  // WHY: A generator command sent while this device captures has wedged real hardware: never send one then.
  if (ui.capture.running()) {
    return;
  }
  for (const std::string& facetName : ui.deviceProperties->facetNames()) {
    if (!gui_properties::isGeneratorFacet(facetName)) {
      continue;
    }
    gui_properties::FacetPropertiesModel* model = ui.deviceProperties->facet(facetName);
    if (model == nullptr) {
      continue;
    }
    if (switchOff) {
      model->resetAllToDefault();
      ui.propertyTextBuffers.clear();
    }
    sendFacetPropertiesNow(ui, facetName, model->valuesToSend());
  }
}

std::expected<acquisition::MemoryCheck, std::string> GuiApp::checkMemory(const gui_capture::CaptureRequest& request) {
  acquisition::AcquisitionSeams seams{*memoryProbe_, *blockSource_};
  seams.samples_per_window = request.samplesPerWindow;
  acquisition::SessionRequest session;
  session.memory_policy = request.request.memory_policy;
  session.memory_budget_bytes = request.request.memory_budget_bytes;
  session.reserve_bytes = request.request.reserve_bytes;
  session.keep_on_memory_full = true;
  return acquisition::checkMemoryBeforeStart(request.request, session, seams);
}

void GuiApp::pressStart(gui_session::SessionId id) {
  const gui_session::Session* session = sessions_.find(id);
  if (session == nullptr || !sessions_.canStart(id)) {
    return;
  }
  SessionUi& ui = sessionUi_[id];
  if (ui.capture.saving()) {
    status(tr("Saving: Start waits until save has ended."));
    return;
  }
  if (!ui.capabilities || !ui.settings) {
    status(ui.capabilitiesProblem.empty() ? std::string(tr("Capture settings of this device are not known yet."))
                                          : trf("This device cannot capture: {}", ui.capabilitiesProblem));
    return;
  }
  auto request = gc::buildCaptureRequest(*ui.settings, *ui.capabilities, hardwareThreads(),
                                         ui.deviceProperties ? ui.deviceProperties->valuesToSend()
                                                             : std::map<std::string, std::vector<ipc::props::PropertyValue>>{});
  if (!request) {
    ui.settingsProblem = request.error();
    return;
  }
  ui.startRequest = std::move(*request);
  ui.alternatives.reset();
  ui.rememberUnsavedChoice = false;
  ui.startFlow.press(ui.capture.hasUnsavedCapture(), policyOf(prefs_.unsavedCapture));
  advanceStartFlow(id, ui);
}

void GuiApp::advanceStartFlow(gui_session::SessionId id, SessionUi& ui) {
  if (const auto remembered = ui.startFlow.takeRememberedPolicy()) {
    prefs_.unsavedCapture = *remembered == gc::UnsavedPolicy::DontSave ? gui_prefs::UnsavedCapturePolicy::DontSave
                                                                       : gui_prefs::UnsavedCapturePolicy::Ask;
    savePreferencesNow();
  }
  if (ui.startFlow.step() == gc::StartFlow::Step::CheckMemory && ui.startRequest) {
    // WHY: Release the shown capture BEFORE the memory check so that its memory counts as free for the new capture.
    if (ui.capture.kept()) {
      const gui_session::Session* session = sessions_.find(id);
      logger_.logf(log::LogLevel::info, "capture", "{}: capture shown is let go for new one",
                   session != nullptr ? session->name : std::to_string(id));
    }
    ui.capture.release();
    ui.keptDeviceChannels.clear();
    dropWaveformCapture(ui);
    letGoDecodes(ui);
    const auto check = checkMemory(*ui.startRequest);
    if (!check && ui.capabilities && ui.settings) {
      ui.alternatives = gc::alternativesThatStore(*ui.settings, *ui.capabilities, [&](const gc::CaptureSettings& s) {
        auto candidate = gc::buildCaptureRequest(s, *ui.capabilities, hardwareThreads());
        return candidate.has_value() && checkMemory(*candidate).has_value();
      });
    }
    ui.startFlow.memoryChecked(check);
  }
  if (ui.startFlow.step() == gc::StartFlow::Step::Start) {
    ui.startFlow.reset();
    startCaptureRun(id, ui);
  }
}

void GuiApp::startCaptureRun(gui_session::SessionId id, SessionUi& ui) {
  const gui_session::Session* session = sessions_.find(id);
  if (session == nullptr || !session->device || !ui.startRequest || pipeHost_ == nullptr) {
    return;
  }
  const ipc::DeviceInfo* opened = router_.openedDevice(session->device->driver, session->device->id);
  if (opened == nullptr || !sessions_.startCapture(id)) {
    status(tr("Device is not open: capture was not started."));
    return;
  }
  const std::string name = session->name;
  const std::string pluginId = session->device->driver + "/" + session->device->id;
  const std::uint64_t run = nextRun_++;
  gc::CaptureRunSetup setup;
  setup.run = run;
  setup.device = host::pipe::PluginDevice{pluginId, *opened};
  setup.transport = &pipeHost_->pluginHost();
  const std::shared_ptr<host::PluginConnection> connection = pipeHost_->pluginHost().connection(pluginId);
  setup.pluginClosed = [connection]() -> std::optional<std::string> {
    return connection ? connection->closedReason() : std::optional<std::string>("driver is not open");
  };
  setup.deviceLost = [connection]() -> std::optional<std::string> {
    return connection ? connection->deviceLostReason() : std::nullopt;
  };
  setup.captureEnded = [this, pluginId](std::uint64_t captureId) { return captureEnds_.ended(pluginId, captureId); };
  setup.request = *ui.startRequest;
  setup.probe = memoryProbe_.get();
  setup.blocks = blockSource_.get();

  ui.slot = std::make_shared<gc::LiveCaptureSlot>();
  ui.running = setup.request;
  ui.capture.started(run);
  dropWaveformCapture(ui);
  letGoDecodes(ui);
  const auto& r = setup.request.request;
  logger_.logf(log::LogLevel::info, "capture", "{}: capture {} started on {}: {} channel(s) at {}, {} samples{}", name,
               run, pluginId, setup.request.deviceChannels.size(), gui_format::formatFrequency(r.sample_rate_milli_hz),
               gui_format::withThousands(setup.request.requestedSamples), r.trigger ? ", triggered" : "");
  std::shared_ptr<gc::LiveCaptureSlot> slot = ui.slot;
  gui_events::GuiEventQueue& queue = eventQueue_;
  ui.runner = std::make_unique<gc::CaptureRunner>([setup, slot, &queue](const gc::RunControl& control) {
    gc::runCapture(setup, control, queue, *slot, acquisition::acquireSession);
  });
}

void GuiApp::pressStop(gui_session::SessionId id) {
  SessionUi& ui = sessionUi_[id];
  if (ui.capture.requestStop() && ui.runner) {
    ui.runner->requestStop();
    const gui_session::Session* session = sessions_.find(id);
    logger_.logf(log::LogLevel::info, "capture", "{}: Stop pressed: what was acquired is kept",
                 session != nullptr ? session->name : std::to_string(id));
  }
}

GuiApp::SessionUi* GuiApp::runOwner(std::uint64_t run, gui_session::SessionId* id) {
  for (auto& [sessionId, ui] : sessionUi_) {
    if (ui.capture.run() == run) {
      if (id != nullptr) {
        *id = sessionId;
      }
      return &ui;
    }
  }
  return nullptr;
}

void GuiApp::captureFinished(std::uint64_t run, const std::expected<acquisition::SessionResult, std::string>& result) {
  gui_session::SessionId id = 0;
  SessionUi* ui = runOwner(run, &id);
  if (ui == nullptr) {
    return;
  }
  if (ui->runner) {
    ui->runner->join();
    ui->runner.reset();
  }
  ui->slot.reset();
  sessions_.endCapture(id);
  gc::CaptureOutcome outcome = gc::outcomeOf(result);
  const gui_session::Session* session = sessions_.find(id);
  const std::string name = session != nullptr ? session->name : std::to_string(id);
  switch (outcome.kind) {
  case gc::OutcomeKind::Complete:
    logger_.logf(log::LogLevel::info, "capture", "{}: capture {} complete: {} samples", name, run,
                 gui_format::withThousands(outcome.storedSamples));
    break;
  case gc::OutcomeKind::ShortMemory:
  case gc::OutcomeKind::ShortStoppedByUser:
  case gc::OutcomeKind::ShortStoppedWithSession:
    logger_.logf(log::LogLevel::warning, "capture", "{}: capture {} kept SHORT ({}): {} of {} samples", name, run,
                 outcome.kind == gc::OutcomeKind::ShortMemory        ? "memory full"
                 : outcome.kind == gc::OutcomeKind::ShortStoppedByUser ? "stopped by user"
                                                                     : "stopped with session",
                 gui_format::withThousands(outcome.storedSamples), gui_format::withThousands(outcome.requestedSamples));
    break;
  case gc::OutcomeKind::Failed:
    logger_.logf(log::LogLevel::error, "capture", "{}: capture {} failed, nothing kept: {}", name, run,
                 outcome.message);
    break;
  }
  ui->keptDeviceChannels = outcome.kept() && ui->running ? ui->running->deviceChannels : std::vector<std::uint32_t>{};
  ui->capturedAt = std::chrono::system_clock::now();
  ui->keptDeviceProperties.clear();
  ui->loadedDecoderSetup.clear();
  ui->loadedDecoderSkipped.clear();
  ui->loadedDecoderVersions.clear();
  gui_decode::recordLoadedSetup(ui->decoders, {}, decoderRegistry());
  if (outcome.kept() && session != nullptr && session->device) {
    if (const ipc::DeviceInfo* opened = router_.openedDevice(session->device->driver, session->device->id)) {
      ui->keptDeviceProperties = gui_session_file::savedDeviceProperties(outcome.sentProperties, *opened);
    }
  }
  const bool liveKept = ui->decodes.captureEnded(outcome);
  ui->running.reset();
  ui->capture.finished(run, std::move(outcome));
  dropWaveformCapture(*ui);
  if (liveKept) {
    showCapturedLanes(*ui);
  } else {
    runDecodersForSession(id, *ui);
  }
}

void GuiApp::stopAllCaptures() {
  for (auto& [id, ui] : sessionUi_) {
    if (ui.runner) {
      ui.runner->requestAbandon();
      ui.runner->join();
      ui.runner.reset();
    }
    ui.slot.reset();
  }
}

void GuiApp::dropWaveformCapture(SessionUi& ui) {
  if (!ui.waveform) {
    return;
  }
  ui.waveform->sources.drop();
  ui.waveform->columns.forget();
}

namespace {

void resetWaveformView(SessionWaveform& w, bool ofCapture) {
  if (w.viewport && w.fitted && w.viewOfCapture) {
    w.carriedZoom = wv::carriedZoomOf(*w.viewport);
  }
  w.viewOfCapture = ofCapture;
  w.columns.forget();
  w.fitted = false;
  w.viewport.reset();
  w.lastTotal = 0;
}

}

void GuiApp::syncWaveform(gui_session::SessionId id, SessionUi& ui,
                          const std::optional<gui_waveform_frame::LiveSource>& live) {
  if (!ui.waveform) {
    ui.waveform = std::make_shared<SessionWaveform>();
    ui.waveform->view = id;
  }
  SessionWaveform& w = *ui.waveform;

  gui_waveform_frame::SourceChoice choice;
  if (live && ui.running) {
    choice.live = *live;
  }
  if (const auto& kept = ui.capture.kept(); kept && kept->buffer) {
    choice.kept = gui_waveform_frame::KeptSource{kept->buffer,
                                                 kept->session,
                                                 kept->requestedSamples,
                                                 ui.keptDeviceChannels,
                                                 kept->extent,
                                                 gui_capture::timeZeroSample(*kept)};
  }
  std::vector<std::uint32_t> devices;
  if (ui.settings) {
    for (std::uint32_t c = 0; c < ui.settings->channels.size(); ++c) {
      if (ui.settings->channels[c]) {
        devices.push_back(c);
      }
    }
  }
  choice.idle = gui_waveform_frame::IdleSource{
    devices, ui.settings && ui.settings->sampleRateMilliHz != 0 ? ui.settings->sampleRateMilliHz : 1'000'000'000,
    ui.settings ? ui.settings->lengthSamples : 0};

  switch (w.sources.sync(choice)) {
  case gui_waveform_frame::SourceChange::ReplacedCapture:
    w.note.clear();
    resetWaveformView(w, true);
    break;
  case gui_waveform_frame::SourceChange::NewExtent:
    w.columns.forget();
    break;
  case gui_waveform_frame::SourceChange::ReplacedIdle:
    resetWaveformView(w, false);
    break;
  case gui_waveform_frame::SourceChange::None:
    break;
  }
  if (w.sources.shows() == gui_waveform_frame::WaveformSourceChain::Shows::Nothing) {
    w.note = ui.capture.lastError() ? trf("Capture failed, nothing kept: {}", *ui.capture.lastError())
             : !ui.capabilitiesProblem.empty() ? trf("This device cannot capture: {}", ui.capabilitiesProblem)
                                               : std::string();
  }
}

void GuiApp::rateCombo(SessionUi& ui, const char* id) {
  gc::CaptureSettings& settings = *ui.settings;
  const std::uint32_t selected = gc::selectedChannelCount(settings);
  const std::vector<gc::RateChoice> choices = gc::rateChoices(*ui.capabilities, selected);
  const std::optional<std::uint64_t> ceiling = gc::rateCeiling(*ui.capabilities, selected);
  const std::string preview = gui_format::formatFrequency(settings.sampleRateMilliHz);
  const bool tooFast = ceiling && settings.sampleRateMilliHz > *ceiling;
  if (tooFast) {
    ImGui::PushStyleColor(ImGuiCol_Text, errorTextColor(darkInUse()));
  }
  if (ImGui::BeginCombo(id, preview.c_str())) {
    for (const gc::RateChoice& choice : choices) {
      const std::string label = gui_format::formatFrequency(choice.milliHz);
      const bool isSelected = choice.milliHz == settings.sampleRateMilliHz;
      if (ImGui::Selectable(label.c_str(), isSelected, choice.allowed ? 0 : ImGuiSelectableFlags_Disabled)) {
        settings.sampleRateMilliHz = choice.milliHz;
      }
      if (!choice.allowed && ceiling) {
        tooltipAnyway(trfn("With {} channel at most {}", "With {} channels at most {}", selected, selected,
                           gui_format::formatFrequency(*ceiling))
                        .c_str());
      }
      if (isSelected) {
        ImGui::SetItemDefaultFocus();
      }
    }
    ImGui::EndCombo();
  }
  if (tooFast) {
    ImGui::PopStyleColor();
    tooltipAnyway(trfn("With {} channel this device samples at most at {}", "With {} channels this device samples at "
                                                                           "most at {}",
                       selected, selected, gui_format::formatFrequency(*ceiling))
                    .c_str());
  } else {
    tooltipAnyway(tr("Sample rate"));
  }
}

void GuiApp::lengthCombo(SessionUi& ui, const char* id) {
  gc::CaptureSettings& settings = *ui.settings;
  const auto label = [&](std::uint64_t n) {
    const std::string duration = durationText(n, settings.sampleRateMilliHz);
    const std::string samples =
      trfn("{} sample", "{} samples", static_cast<unsigned long>((std::min)(n, std::uint64_t{0xFFFFFFFF})),
           gui_format::withThousands(n));
    return duration.empty() ? samples : trf("{} ({})", samples, duration);
  };
  const ImGuiStyle& style = ImGui::GetStyle();
  const float arrowWidth = ImGui::GetFrameHeight();
  const float total = ImGui::CalcItemWidth();
  ImGui::PushID(id);
  if (!ui.lengthEditing) {
    const std::string shown = gui_format::withThousands(settings.lengthSamples);
    std::snprintf(ui.lengthText.data(), ui.lengthText.size(), "%s", shown.c_str());
  }
  ImGui::SetNextItemWidth((std::max)(1.0f, total - arrowWidth - style.ItemInnerSpacing.x));
  const gui_text_field::Commit commit = gui_text_field::committedInputText(
    "##length_text", ui.lengthText.data(), ui.lengthText.size(), ImGuiInputTextFlags_AutoSelectAll);
  const bool editing = ImGui::IsItemActive();
  ui.lengthEditing = editing;
  const ImVec2 fieldMin = ImGui::GetItemRectMin();
  const ImVec2 fieldMax = ImGui::GetItemRectMax();
  tooltipAnyway(tr("Samples to capture: choose from list, or type count (1.5M) or time at sample rate (2 s, 50 ms)"));
  if (commit != gui_text_field::Commit::None) {
    if (const auto typed = gc::parseLength(ui.lengthText.data(), settings.sampleRateMilliHz)) {
      settings.lengthSamples = *typed;
    } else {
      status(trf("Length not changed: {}", typed.error()));
    }
  }
  if (!editing) {
    const std::string duration = durationText(settings.lengthSamples, settings.sampleRateMilliHz);
    if (!duration.empty()) {
      const std::string text = "(" + duration + ")";
      const float width = ImGui::CalcTextSize(text.c_str()).x;
      const float numberEnd = fieldMin.x + style.FramePadding.x + ImGui::CalcTextSize(ui.lengthText.data()).x;
      const float textX = fieldMax.x - style.FramePadding.x - width;
      if (textX > numberEnd + style.ItemInnerSpacing.x) {
        ImGui::GetWindowDrawList()->AddText(ImVec2(textX, fieldMin.y + style.FramePadding.y),
                                            ImGui::GetColorU32(ImGuiCol_TextDisabled), text.c_str());
      }
    }
  }
  ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
  if (ImGui::ArrowButton("##length_choices", ImGuiDir_Down)) {
    ImGui::OpenPopup("##length_list");
  }
  ImGui::SetNextWindowPos(ImVec2(fieldMin.x, fieldMax.y));
  ImGui::SetNextWindowSizeConstraints(ImVec2(total, 0.0f), ImVec2(FLT_MAX, FLT_MAX));
  if (ImGui::BeginPopup("##length_list")) {
    for (const std::uint64_t n : gc::kLengthChoices) {
      const bool isSelected = n == settings.lengthSamples;
      if (ImGui::Selectable(label(n).c_str(), isSelected)) {
        settings.lengthSamples = n;
      }
      if (isSelected) {
        ImGui::SetItemDefaultFocus();
      }
    }
    ImGui::EndPopup();
  }
  ImGui::PopID();
}

namespace {

struct StatusText {
  std::string text;
  std::string tip;
  bool problem = false;
};

StatusText statusOf(const gc::PanelCapture& capture) {
  StatusText status;
  std::string& text = status.text;
  std::string& tip = status.tip;
  if (capture.running()) {
    if (capture.stopRequested()) {
      text = tr("Stopping...");
    } else if (const auto& p = capture.lastProgress()) {
      if (p->armed) {
        text = trf("Waiting for trigger ({} s)", p->armed->waited_ms / 1000);
      } else {
        text = trf("Capturing: {}", gui_format::formatWholePercent(p->stored_samples, p->requested_samples));
      }
    } else {
      text = tr("Starting...");
    }
    if (!capture.retries().empty()) {
      const acquisition::AcquireRetry& retry = capture.retries().back();
      text += "  " + trf("(attempt {} of {})", retry.attempt, retry.attempts_allowed);
      for (const acquisition::AcquireRetry& r : capture.retries()) {
        tip += trf("Attempt {} lost its start: {}", r.attempt - 1, r.lost) + "\n";
      }
    }
  } else if (const auto& kept = capture.kept()) {
    const std::string counts = trf("{} of {} samples", gui_format::withThousands(kept->storedSamples),
                                   gui_format::withThousands(kept->requestedSamples));
    switch (kept->kind) {
    case gc::OutcomeKind::Complete:
      text = trfn("{} sample", "{} samples",
                  static_cast<unsigned long>((std::min)(kept->storedSamples, std::uint64_t{0xFFFFFFFF})),
                  gui_format::withThousands(kept->storedSamples));
      break;
    case gc::OutcomeKind::ShortMemory:
      text = trf("Short capture, memory full: {}", counts);
      status.problem = true;
      break;
    case gc::OutcomeKind::ShortStoppedByUser:
      text = trf("Short capture, stopped by user: {}", counts);
      status.problem = true;
      break;
    case gc::OutcomeKind::ShortStoppedWithSession:
      text = trf("Short capture, stopped with session: {}", counts);
      status.problem = true;
      break;
    case gc::OutcomeKind::Failed:
      break;
    }
    if (kept->requestedPostTriggerSamples && kept->storedPostTriggerSamples) {
      tip = trf("After trigger: {} of {} samples", gui_format::withThousands(*kept->storedPostTriggerSamples),
                gui_format::withThousands(*kept->requestedPostTriggerSamples));
    }
    if (capture.hasUnsavedCapture()) {
      tip += (tip.empty() ? "" : "\n") + std::string(tr("Not saved"));
    }
  } else if (const auto& error = capture.lastError()) {
    text = tr("Capture failed: nothing kept");
    tip = *error;
    status.problem = true;
  }
  return status;
}

}

std::string GuiApp::captureStatusText(gui_session::SessionId id) {
  const auto ui = sessionUi_.find(id);
  return ui == sessionUi_.end() ? std::string() : statusOf(ui->second.capture).text;
}

void GuiApp::drawCaptureStatus(SessionUi& ui) {
  const StatusText status = statusOf(ui.capture);
  if (status.text.empty()) {
    return;
  }
  ImGui::SameLine(0, ImGui::GetStyle().ItemSpacing.x * 3);
  ImGui::AlignTextToFramePadding();
  if (status.problem) {
    ImGui::PushStyleColor(ImGuiCol_Text, errorTextColor(darkInUse()));
  }
  ImGui::TextUnformatted(status.text.c_str());
  if (status.problem) {
    ImGui::PopStyleColor();
  }
  const std::string tip = status.tip.empty() ? status.text : status.text + "\n" + status.tip;
  tooltipAnyway(tip.c_str());
}

void GuiApp::drawStartModals(const gui_session::Session& session, SessionUi& ui) {
  using Step = gc::StartFlow::Step;
  const float width = ImGui::GetFontSize() * 28.0f;
  const auto open = [](const char* id, bool wanted) {
    if (wanted && !ImGui::IsPopupOpen(id)) {
      ImGui::OpenPopup(id);
    }
  };
  const Step step = ui.startFlow.step();

  const std::string unsavedId = withId(tr("Save unsaved changes?"), "start/unsaved");
  open(unsavedId.c_str(), step == Step::AskUnsaved);
  ImGui::SetNextWindowSize(ImVec2(width, 0));
  if (ImGui::BeginPopupModal(unsavedId.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::TextWrapped("%s", trf("Capture in {} is not saved. New capture replaces it.", session.name).c_str());
    const bool saving = ui.capture.saving();
    ImGui::BeginDisabled(saving);
    ImGui::Checkbox(tr("Remember my choice and do not ask again"), &ui.rememberUnsavedChoice);
    ImGui::EndDisabled();
    if (saving) {
      ImGui::TextUnformatted(tr("Saving..."));
    } else if (const auto& error = ui.capture.lastSaveError()) {
      ImGui::PushStyleColor(ImGuiCol_Text, errorTextColor(darkInUse()));
      ImGui::TextWrapped("%s", trf("Not saved: {}", *error).c_str());
      ImGui::PopStyleColor();
    }
    ImGui::Separator();
    ImGui::BeginDisabled(saving);
    if (ImGui::Button(tr("Save"))) {
      requestSave(session.id, true, gui_session_file::SaveCommand::Save);
    }
    tooltipAnyway(tr("Save capture, then start new one"));
    ImGui::SameLine();
    if (ImGui::Button(tr("Don't save"))) {
      ui.startFlow.answerUnsaved(gc::UnsavedChoice::DontSave, ui.rememberUnsavedChoice);
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button(tr("Cancel")) || (!saving && ImGui::IsKeyPressed(ImGuiKey_Escape))) {
      ui.startFlow.answerUnsaved(gc::UnsavedChoice::Cancel, false);
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();
    if (ui.startFlow.step() != Step::AskUnsaved) {
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }

  const std::string anywayId = withId(tr("Start anyway?"), "start/anyway");
  open(anywayId.c_str(), step == Step::AskStartAnyway);
  ImGui::SetNextWindowSize(ImVec2(width, 0));
  if (ImGui::BeginPopupModal(anywayId.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::TextWrapped("%s", tr("Probably not enough memory for whole capture."));
    if (const auto floor = ui.startFlow.floor(); floor && ui.startRequest) {
      const std::string duration = durationText(*floor, ui.startRequest->request.sample_rate_milli_hz);
      ImGui::TextWrapped("%s", (ui.startRequest->request.trigger
                                  ? trf("All pre-trigger samples and at least {} samples after trigger ({}) fit "
                                        "even if nothing compresses; usually more fit.",
                                        gui_format::withThousands(*floor), duration)
                                  : trf("At least {} samples ({}) fit even if nothing compresses, unless memory is "
                                        "taken meanwhile; usually more fit.",
                                        gui_format::withThousands(*floor), duration))
                                 .c_str());
    }
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", ui.startFlow.message().c_str());
    ImGui::PopStyleColor();
    ImGui::Separator();
    if (ImGui::Button(tr("Start anyway"))) {
      ui.startFlow.answerStartAnyway(true);
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button(tr("Cancel")) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
      ui.startFlow.answerStartAnyway(false);
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }

  const std::string refusalId = withId(tr("Not enough memory"), "start/refusal");
  open(refusalId.c_str(), step == Step::ShowRefusal);
  ImGui::SetNextWindowSize(ImVec2(width, 0));
  if (ImGui::BeginPopupModal(refusalId.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::TextWrapped("%s", tr("Not even smallest part of this capture fits in memory free now, so nothing "
                                "was started."));
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", ui.startFlow.message().c_str());
    ImGui::PopStyleColor();
    ImGui::TextUnformatted(tr("What would work:"));
    if (ui.alternatives) {
      if (ui.alternatives->channels) {
        ImGui::BulletText("%s", trfn("At most {} channel", "At most {} channels", *ui.alternatives->channels,
                                     *ui.alternatives->channels)
                                  .c_str());
      }
      if (ui.alternatives->rateMilliHz) {
        ImGui::BulletText("%s", trf("Sample rate of {} or lower",
                                    gui_format::formatFrequency(*ui.alternatives->rateMilliHz))
                                  .c_str());
      }
      if (ui.alternatives->lengthSamples) {
        ImGui::BulletText("%s", trf("Length of {} samples or fewer",
                                    gui_format::withThousands(*ui.alternatives->lengthSamples))
                                  .c_str());
      }
    }
    ImGui::BulletText("%s", tr("More free memory: close other programs or other sessions' captures"));
    ImGui::Separator();
    if (ImGui::Button(tr("OK")) || ImGui::IsKeyPressed(ImGuiKey_Escape) || ImGui::IsKeyPressed(ImGuiKey_Enter)) {
      ui.startFlow.dismiss();
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }

  const std::string settingsId = withId(tr("Cannot start capture"), "start/settings");
  open(settingsId.c_str(), ui.settingsProblem.has_value());
  ImGui::SetNextWindowSize(ImVec2(width, 0));
  if (ImGui::BeginPopupModal(settingsId.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    if (ui.settingsProblem) {
      ImGui::TextWrapped("%s", settingsProblemText(*ui.settingsProblem).c_str());
    }
    ImGui::Separator();
    if (ImGui::Button(tr("OK")) || ImGui::IsKeyPressed(ImGuiKey_Escape) || ImGui::IsKeyPressed(ImGuiKey_Enter)) {
      ui.settingsProblem.reset();
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }

  if (ui.startFlow.step() != step &&
      (ui.startFlow.step() == Step::CheckMemory || ui.startFlow.step() == Step::Start)) {
    advanceStartFlow(session.id, ui);
  }
}

std::vector<gc::ClosingPanel> GuiApp::closingPanels() const {
  std::vector<gc::ClosingPanel> panels;
  for (const gui_session::Session& session : sessions_.sessions()) {
    gc::ClosingPanel panel;
    panel.id = session.id;
    panel.name = session.name;
    if (const auto ui = sessionUi_.find(session.id); ui != sessionUi_.end()) {
      panel.unsaved = gc::panelUnsavedMark(ui->second.capture);
      panel.saving = ui->second.capture.saving();
      panel.running = ui->second.capture.running();
    }
    panel.saving = panel.saving ||
                   (fileRequest_ && fileRequest_->kind == FileRequest::Kind::Save && fileRequest_->session == session.id) ||
                   (replaceQuestion_ && replaceQuestion_->session == session.id);
    panel.running = panel.running || !sessions_.canClose(session.id);
    panels.push_back(std::move(panel));
  }
  return panels;
}

gc::UnsavedPolicy GuiApp::rememberedUnsavedPolicy() const { return policyOf(prefs_.unsavedCapture); }

void GuiApp::requestPanelClose(gui_session::SessionId id) {
  const gc::UnsavedPolicy remembered = rememberedUnsavedPolicy();
  if (!closeFlow_.requestPanelClose(id, remembered)) {
    return;
  }
  if (const auto ui = sessionUi_.find(id);
      remembered == gc::UnsavedPolicy::DontSave && ui != sessionUi_.end() && ui->second.capture.hasUnsavedCapture()) {
    const gui_session::Session* session = sessions_.find(id);
    logger_.logf(log::LogLevel::info, "gui", "{}: closed without saving, Don't save is remembered",
                 session != nullptr ? session->name : std::to_string(id));
  }
  advanceCloseFlow();
}

void GuiApp::requestAppClose() {
  const gc::UnsavedPolicy remembered = rememberedUnsavedPolicy();
  if (!closeFlow_.requestAppClose(remembered)) {
    logger_.log(log::LogLevel::info, "gui", "Close asked while panel's own close question is open: answer it first");
    return;
  }
  logger_.log(log::LogLevel::info, "gui",
              remembered == gc::UnsavedPolicy::DontSave ? "Close asked: Don't save is remembered, nothing asked"
                                                        : "Close asked");
  advanceCloseFlow();
}

bool GuiApp::advanceCloseFlow() {
  using Kind = gc::CloseFlow::Action::Kind;
  bool changed = false;
  for (int round = 0; round < 4; ++round) {
    const bool app = closeFlow_.closingApp();
    const gc::CloseFlow::Action action = closeFlow_.next(closingPanels());
    if (action != lastCloseAction_) {
      changed = true;
      lastCloseAction_ = action;
    }
    switch (action.kind) {
    case Kind::None:
    case Kind::Ask:
    case Kind::Wait:
      return changed;
    case Kind::StartSave: {
      const gui_session::Session* session = sessions_.find(action.panel);
      logger_.logf(log::LogLevel::info, "gui", "{}: saving before close",
                   session != nullptr ? session->name : std::to_string(action.panel));
      requestSave(action.panel, false, gui_session_file::SaveCommand::Save);
      continue;
    }
    case Kind::ClosePanel:
      closeSession(action.panel);
      return true;
    case Kind::CloseApp:
      logger_.log(log::LogLevel::info, "gui", "Closing");
      closeConfirmed_ = true;
      return true;
    case Kind::Abort:
      logger_.log(log::LogLevel::info, "gui", app ? "Close cancelled: bitoscope stays open" : "Close cancelled: panel stays open");
      return true;
    }
  }
  return changed;
}

void GuiApp::drawCloseModal() {
  const std::string id = withId(tr("Save unsaved changes?"), "close/unsaved");
  const std::optional<std::uint64_t> asked = closeFlow_.question();
  const gui_session::Session* session = asked ? sessions_.find(*asked) : nullptr;
  if (session != nullptr && !ImGui::IsPopupOpen(id.c_str()) && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) {
    ImGui::OpenPopup(id.c_str());
  }
  ImGui::SetNextWindowSize(ImVec2(ImGui::GetFontSize() * 28.0f, 0));
  if (!ImGui::BeginPopupModal(id.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    return;
  }
  if (session == nullptr) {
    ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
    return;
  }
  const bool app = closeFlow_.closingApp();
  ImGui::TextWrapped("%s", trf("{} has unsaved changes.", session->name).c_str());
  ImGui::TextWrapped("%s", app ? tr("Closing bitoscope loses them.") : tr("Closing panel loses them."));
  const bool saving = closeFlow_.savingQuestion();
  if (saving) {
    ImGui::TextUnformatted(tr("Saving..."));
  }
  ImGui::Separator();
  ImGui::BeginDisabled(saving);
  std::optional<gc::CloseAnswer> answer;
  if (ImGui::Button(tr("Save"))) {
    answer = gc::CloseAnswer::Save;
  }
  tooltipAnyway(app ? tr("Save, then go on closing") : tr("Save, then close panel"));
  ImGui::SameLine();
  if (ImGui::Button(tr("Don't save"))) {
    answer = gc::CloseAnswer::DontSave;
  }
  ImGui::SameLine();
  if (ImGui::Button(tr("Cancel")) || (!saving && ImGui::IsKeyPressed(ImGuiKey_Escape))) {
    answer = gc::CloseAnswer::Cancel;
  }
  if (app) {
    tooltipAnyway(tr("Keep bitoscope open"));
  }
  ImGui::EndDisabled();
  if (answer) {
    closeFlow_.answer(*answer);
    advanceCloseFlow();
  }
  if (!closeFlow_.question()) {
    ImGui::CloseCurrentPopup();
  }
  ImGui::EndPopup();
}

}
