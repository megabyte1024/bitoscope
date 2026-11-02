/*
 * SPDX-License-Identifier: MIT
 */

#include <imgui.h>

#include <device_label.h>
#include <facet_access.h>
#include <firmware_update_model.h>
#include <pipe_device_host.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <format>
#include <string>
#include <vector>

#include "gui_app.h"
#include "platform_services.h"
#include "theme.h"
#include "tr.h"

namespace bitoscope::gui {

namespace {

namespace fw = bitoscope::ipc::firmware;
namespace gf = bitoscope::gui_firmware;

constexpr const char* kFirmwareId = "firmware_window";

struct Candidate {
  std::string driver;
  std::string deviceId;
  std::string label;
  bool capturing = false;
};

void copyText(std::array<char, 1024>& into, const std::string& text) {
  std::snprintf(into.data(), into.size(), "%s", text.c_str());
}

}

void GuiApp::openFirmwareWindow() {
  if (firmwareWindow_) {
    return;
  }
  firmwareWindow_.emplace();
  firmwareToOpen_ = true;
}

void GuiApp::beginFirmwareHold() {
  FirmwareWindow& window = *firmwareWindow_;
  if (window.flaggedUpdating) {
    return;
  }
  if (router_.isOpen(window.driver, window.deviceId)) {
    applyRouterOutput(router_.holdOpen(window.driver, window.deviceId));
    window.held = true;
  }
  sessions_.setDeviceUpdating(window.driver, window.deviceId, true);
  window.flaggedUpdating = true;
}

void GuiApp::endFirmwareHold() {
  FirmwareWindow& window = *firmwareWindow_;
  if (!window.flaggedUpdating) {
    return;
  }
  sessions_.setDeviceUpdating(window.driver, window.deviceId, false);
  window.flaggedUpdating = false;
  if (window.held) {
    window.held = false;
    applyRouterOutput(router_.releaseHold(window.driver, window.deviceId));
  }
}

void GuiApp::closeFirmwareWindow() {
  if (!firmwareWindow_) {
    return;
  }
  FirmwareWindow& window = *firmwareWindow_;
  window.model.reset();
  endFirmwareHold();
  if (window.opener.joinable()) {
    window.opener.join();
  }
  if (window.ownedConnection && pipeHost_ != nullptr && !window.driverDeviceId.empty()) {
    pipeHost_->driverHost().close(window.driverDeviceId);
  }
  firmwareWindow_.reset();
}

bool GuiApp::updateFirmwareWindow() {
  if (!firmwareWindow_) {
    return false;
  }
  FirmwareWindow& window = *firmwareWindow_;
  bool changed = false;

  if (window.page == FirmwareWindow::Page::Opening && window.opening) {
    std::optional<std::expected<host::pipe::DriverDevice, std::string>> result;
    {
      std::lock_guard lock(window.opening->mutex);
      result = window.opening->result;
    }
    if (result) {
      if (window.opener.joinable()) {
        window.opener.join();
      }
      window.opening.reset();
      changed = true;
      auto client = result->has_value() && pipeHost_ != nullptr
                      ? acquisition::makeFirmwareUpdateClient(**result, pipeHost_->driverHost())
                      : std::expected<host::pipe::FirmwareUpdateClient, std::string>(
                          std::unexpected(result->has_value() ? std::string("GUI has no driver host") : result->error()));
      if (!client) {
        window.error = client.error();
        window.page = FirmwareWindow::Page::ChooseDevice;
        if (window.ownedConnection && pipeHost_ != nullptr) {
          pipeHost_->driverHost().close(window.driverDeviceId);
        }
        window.ownedConnection = false;
      } else {
        window.driverDeviceId = (*result)->driver_device_id;
        window.model = std::make_unique<gf::FirmwareUpdateModel>(pipeHost_->driverHost(), window.driverDeviceId,
                                                                 client->info(), firmwareEvents_);
        window.seenStage = gf::Stage::Idle;
        window.page = FirmwareWindow::Page::Update;
        if (window.path[0] != '\0') {
          window.model->chooseFile(window.path.data());
        }
      }
    }
  }

  if (window.model) {
    window.model->pollStatus();
    const gf::ModelView view = window.model->view();
    if (view.stage != window.seenStage) {
      if (view.stage == gf::Stage::Refused && !view.message.empty()) {
        logger_.logf(log::LogLevel::warning, "firmware", "Firmware file or update refused: {}", view.message);
      }
      if (view.stage == gf::Stage::Running) {
        beginFirmwareHold();
      } else if (window.flaggedUpdating) {
        endFirmwareHold();
      }
      window.seenStage = view.stage;
      changed = true;
    }
    if (view.stage == gf::Stage::Finished && view.finished) {
      const int resends = view.progress ? view.progress->resends : 0;
      logger_.logf(view.finished->outcome == fw::Outcome::done ? log::LogLevel::info : log::LogLevel::warning,
                   "firmware", "Firmware update ended: {}; {} transfers repeated", view.finished->message, resends);
      window.lastResult = FirmwareWindow::LastResult{*view.finished, window.leavingUpdateMode};
      window.leavingUpdateMode = false;
      window.runPage = false;
      window.browse = false;
      window.model.reset();
      if (window.ownedConnection && pipeHost_ != nullptr) {
        pipeHost_->driverHost().close(window.driverDeviceId);
      }
      window.ownedConnection = false;
      window.page = FirmwareWindow::Page::ChooseDevice;
      return true;
    }
    if (window.browse) {
      window.browse = false;
      PlatformServices::FileDialog dialog;
      dialog.title = tr("Choose firmware file");
      dialog.fileTypeName = tr("Firmware file");
      dialog.allFilesName = tr("All files");
      dialog.extension = "bin";
      const auto chosen = platform_.askOpenPath(dialog);
      if (!chosen) {
        window.error = trf("File dialog could not be shown: {}", chosen.error());
      } else if (*chosen) {
        copyText(window.path, (*chosen)->string());
        window.error.clear();
        window.model->chooseFile((*chosen)->string());
      }
      changed = true;
    }
  }
  return changed;
}

void GuiApp::drawFirmwareWindow() {
  const std::string title = withId(tr("Update firmware"), kFirmwareId);
  if (firmwareToOpen_) {
    ImGui::OpenPopup(title.c_str());
    firmwareToOpen_ = false;
  }
  if (!firmwareWindow_) {
    return;
  }
  const float em = ImGui::GetFontSize();
  ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(em * 36.0f, em * 24.0f), ImGuiCond_Appearing);
  const ImGuiWindowFlags flags =
    ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
  if (!ImGui::BeginPopupModal(title.c_str(), nullptr, flags)) {
    closeFirmwareWindow();
    return;
  }
  FirmwareWindow& window = *firmwareWindow_;
  const bool dark = darkInUse();
  const auto errorText = [&](const std::string& text) {
    ImGui::PushStyleColor(ImGuiCol_Text, errorTextColor(dark));
    ImGui::TextWrapped("%s", text.c_str());
    ImGui::PopStyleColor();
  };

  gf::ModelView view;
  if (window.model) {
    view = window.model->view();
  }
  const bool running = view.stage == gf::Stage::Running || view.stage == gf::Stage::Finished;
  const bool checking = view.stage == gf::Stage::Working;
  const bool onFirstStep = window.page != FirmwareWindow::Page::Update || !window.model;
  const bool onRunStep = !onFirstStep && (window.runPage || running);
  const int step = onFirstStep ? 1 : onRunStep ? 3 : 2;

  std::vector<Candidate> candidates;
  if (onFirstStep) {
    for (const gui_devices::DeviceRouter::FacetDevice& device :
         router_.devicesOffering(host::pipe::kFirmwareUpdateFacetName)) {
      const bool capturing = std::ranges::any_of(sessions_.sessions(), [&](const gui_session::Session& s) {
        return s.running && s.device && s.device->driver == device.driver && s.device->id == device.deviceId;
      });
      candidates.push_back(Candidate{device.driver, device.deviceId,
                                   gui_devices::deviceLabel(gui_devices::shownDriverName(router_.catalog(), device.driver),
                                                            device.name, device.deviceId),
                                   capturing});
    }
    window.pickedIndex = (std::min)(window.pickedIndex, candidates.empty() ? std::size_t{0} : candidates.size() - 1);
  }
  const bool opening = window.page == FirmwareWindow::Page::Opening;
  const bool canPickDevice = onFirstStep && !opening && !candidates.empty() && !candidates[window.pickedIndex].capturing;

  const auto openPicked = [&] {
    const Candidate& picked = candidates[window.pickedIndex];
    window.driver = picked.driver;
    window.deviceId = picked.deviceId;
    window.deviceName = picked.label;
    window.error.clear();
    window.lastResult.reset();
    window.ownedConnection = !router_.isOpen(picked.driver, picked.deviceId);
    window.driverDeviceId = picked.driver + "/" + picked.deviceId;
    window.page = FirmwareWindow::Page::Opening;
    window.opening = std::make_shared<FirmwareOpening>();
    host::PipeDriverHost* driverHost = pipeHost_ != nullptr ? &pipeHost_->driverHost() : nullptr;
    window.opener = std::jthread([opening = window.opening, driverHost, driver = picked.driver, id = picked.deviceId] {
      std::expected<host::pipe::DriverDevice, std::string> result =
        std::unexpected(std::string("GUI has no driver host"));
      if (driverHost != nullptr) {
        auto opened = driverHost->open(driver, id);
        const auto connection = opened ? driverHost->connection(*opened) : nullptr;
        const auto handshake = connection ? connection->device() : std::nullopt;
        if (!opened) {
          result = std::unexpected(opened.error().message);
        } else if (!handshake) {
          result = std::unexpected(std::string("driver made no handshake"));
        } else {
          result = host::pipe::DriverDevice{*opened, *handshake};
        }
      }
      std::lock_guard lock(opening->mutex);
      opening->result = std::move(result);
    });
  };

  if (step == 1) {
    ImGui::TextUnformatted(tr("Step 1 of 3: Choose analyzer"));
  } else if (step == 2) {
    ImGui::TextUnformatted(tr("Step 2 of 3: Choose firmware file"));
  } else {
    ImGui::TextUnformatted(tr("Step 3 of 3: Install firmware"));
  }
  ImGui::Separator();

  bool close = false;
  const float footer = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;
  if (ImGui::BeginChild("page", ImVec2(0, -footer))) {
    if (step == 1) {
      if (opening) {
        ImGui::TextUnformatted(trf("Connecting to {}...", window.deviceName).c_str());
      } else {
        if (window.lastResult) {
          const fw::Finished& done = window.lastResult->finished;
          const bool installed = done.outcome == fw::Outcome::done && !window.lastResult->leftUpdateMode;
          switch (done.outcome) {
            case fw::Outcome::done:
              ImGui::TextWrapped("%s", window.lastResult->leftUpdateMode
                                         ? tr("Analyzer left update mode and runs its firmware again.")
                                         : tr("Firmware installed. Analyzer restarted with new firmware."));
              break;
            case fw::Outcome::cancelled:
              errorText(tr("Update stopped."));
              break;
            case fw::Outcome::needsReplug:
              errorText(tr("Analyzer stopped responding. Unplug it and plug it in again."));
              break;
            case fw::Outcome::failed:
              errorText(tr("Update did not succeed."));
              break;
          }
          if (!done.canCapture) {
            errorText(tr("Analyzer cannot capture until firmware is installed completely. Run update again: this "
                         "repairs it."));
          }
          if (!installed) {
            ImGui::TextWrapped("%s", tr("To update, choose analyzer below once it appears again (some seconds)."));
          }
          ImGui::Separator();
        } else {
          ImGui::TextWrapped("%s", tr("Choose analyzer to update."));
        }
        ImGui::Spacing();
        if (candidates.empty()) {
          ImGui::PushTextWrapPos(0.0f);
          ImGui::TextDisabled("%s", tr("No analyzer that bitoscope can update is connected. Connect FNIRSI DLA "
                                       "analyzer: it appears here after some seconds."));
          ImGui::PopTextWrapPos();
        }
        for (std::size_t i = 0; i < candidates.size(); ++i) {
          const Candidate& c = candidates[i];
          ImGui::PushID(static_cast<int>(i));
          ImGui::BeginDisabled(c.capturing);
          if (ImGui::Selectable(c.label.c_str(), window.pickedIndex == i,
                                ImGuiSelectableFlags_AllowDoubleClick)) {
            window.pickedIndex = i;
            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
              openPicked();
            }
          }
          ImGui::EndDisabled();
          if (c.capturing) {
            ImGui::SameLine();
            ImGui::TextDisabled("%s", tr("capturing now: stop capture first"));
          }
          ImGui::PopID();
        }
        if (!window.error.empty()) {
          errorText(window.error);
        }
      }
    } else if (step == 2) {
      gf::FirmwareUpdateModel& model = *window.model;
      ImGui::TextUnformatted(trf("Analyzer: {}", window.deviceName).c_str());
      if (view.inBootloader) {
        ImGui::TextWrapped("%s", tr("Analyzer is in update mode: it waits for new firmware and cannot capture. In "
                                    "this mode it cannot report its model, so choose it below."));
        ImGui::TextWrapped("%s", tr("Not updating now? Press Leave update mode: analyzer restarts with firmware it "
                                    "already has."));
      }
      ImGui::Spacing();
      ImGui::BeginDisabled(checking);
      if (view.inBootloader) {
        ImGui::TextUnformatted(tr("Channels on your analyzer:"));
        ImGui::SameLine();
        if (ImGui::RadioButton(tr("16 channels"), view.chosenModel == "16")) {
          model.setChosenModel("16");
        }
        ImGui::SameLine();
        if (ImGui::RadioButton(tr("32 channels"), view.chosenModel == "32")) {
          model.setChosenModel("32");
        }
      }
      ImGui::TextUnformatted(tr("Firmware file:"));
      ImGui::SetNextItemWidth(-em * 16.0f);
      ImGui::InputText("##firmware_path", window.path.data(), window.path.size());
      ImGui::SameLine();
      if (ImGui::Button(tr("Browse..."))) {
        window.browse = true;
      }
      ImGui::SameLine();
      if (ImGui::Button(tr("Check file")) && window.path[0] != '\0') {
        window.error.clear();
        model.chooseFile(window.path.data());
      }
      ImGui::EndDisabled();
      if (!window.error.empty()) {
        errorText(window.error);
      }
      if (view.stage == gf::Stage::Idle && window.error.empty()) {
        ImGui::TextWrapped("%s", tr("Choose FNIRSI firmware file (.bin), then press Check file."));
      } else if (view.stage == gf::Stage::Working || view.stage == gf::Stage::Ready) {
        ImGui::TextWrapped("%s", view.message.c_str());
      } else if (view.stage == gf::Stage::Refused && view.verdict == fw::Verdict::unreadable) {
        errorText(tr("This file is not DLA firmware file."));
      } else if (view.stage == gf::Stage::Refused && view.verdict == fw::Verdict::unknownFile) {
        errorText(tr("This file is not one of FNIRSI firmware files known to bitoscope. Install it only if you are "
                     "sure it is made for your analyzer."));
      } else if (view.stage == gf::Stage::Refused) {
        errorText(view.message);
      }
      const bool riskUnknown = view.verdict == fw::Verdict::unknownFile || view.confirmUnknownFile;
      const bool riskOther = view.verdict == fw::Verdict::otherModel || view.confirmOtherModel;
      if ((view.stage == gf::Stage::Ready || view.stage == gf::Stage::Refused) && (riskUnknown || riskOther)) {
        if (riskUnknown) {
          bool confirmed = view.confirmUnknownFile;
          if (ImGui::Checkbox(tr("Install this file anyway"), &confirmed)) {
            model.setConfirmUnknownFile(confirmed);
          }
        }
        if (riskOther) {
          bool confirmed = view.confirmOtherModel;
          if (ImGui::Checkbox(tr("Install it anyway, although it is made for other model (can damage analyzer)"), &confirmed)) {
            model.setConfirmOtherModel(confirmed);
          }
        }
      }
    } else {
      ImGui::TextUnformatted(trf("Analyzer: {}", window.deviceName).c_str());
      if (!window.leavingUpdateMode) {
        ImGui::TextUnformatted(trf("Firmware file: {}", std::string(window.path.data())).c_str());
      }
      if (view.stage == gf::Stage::Ready) {
        ImGui::TextWrapped("%s", view.message.c_str());
      }
      if (view.stage == gf::Stage::Refused) {
        errorText(tr("Update could not start. Try again; if it keeps failing, unplug analyzer and plug it in "
                     "again."));
      }
      if (view.stage != gf::Stage::Finished && !window.leavingUpdateMode) {
        ImGui::Spacing();
        ImGui::TextWrapped("%s", tr("Update takes about 5 minutes. Do not unplug analyzer until it ends."));
      }
      if (running) {
        ImGui::Separator();
        if (view.progress) {
          const fw::Progress& progress = *view.progress;
          const float fraction = progress.bytesTotal == 0
                                   ? 0.0f
                                   : static_cast<float>(static_cast<double>(progress.bytesSent) /
                                                        static_cast<double>(progress.bytesTotal));
          ImGui::ProgressBar(fraction, ImVec2(-1.0f, 0.0f));
          if (progress.phase == fw::Phase::sending) {
            ImGui::TextUnformatted(tr("Installing firmware..."));
          } else if (progress.phase == fw::Phase::finishing) {
            ImGui::TextUnformatted(tr("Finishing: analyzer restarts with new firmware"));
          } else {
            ImGui::TextUnformatted(tr("Getting analyzer ready..."));
          }
          if (progress.attempt > 1) {
            ImGui::TextUnformatted(
              trf("Problem during update: starting again (try {} of {})", progress.attempt, progress.attempts).c_str());
          }
        } else if (view.stage == gf::Stage::Running) {
          ImGui::TextUnformatted(view.message.c_str());
        }
      }
      if (view.stage == gf::Stage::Running && view.cancelNeedsConfirmation) {
        errorText(tr("If you stop now, analyzer cannot capture until you run update again to its end."));
      }
      if (view.stage != gf::Stage::Running && view.stage != gf::Stage::Finished && !window.leavingUpdateMode) {
        ImGui::Spacing();
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("%s", tr("You can stop update. If stopping would leave analyzer unable to capture, "
                                     "you are asked first; running update again repairs it."));
        ImGui::PopTextWrapPos();
      }
    }
  }
  ImGui::EndChild();
  ImGui::Separator();

  const bool startingProgram = step == 2 && view.inBootloader;
  const bool confirming = step == 3 && view.stage == gf::Stage::Running && view.cancelNeedsConfirmation;
  const char* back = tr("< Back");
  const char* next = tr("Next >");
  const char* startUpdate = tr("Start update");
  const char* startProgram = tr("Leave update mode");
  const char* stopAnyway = tr("Stop anyway");
  const char* cancel = step == 3 && view.stage == gf::Stage::Running ? tr("Stop update")
                       : step == 1 && window.lastResult                 ? tr("Close")
                                                                        : tr("Cancel");
  std::vector<const char*> labels{back, next, startUpdate, cancel};
  if (startingProgram) {
    labels.push_back(startProgram);
  }
  if (confirming) {
    labels.push_back(stopAnyway);
  }
  const ImGuiStyle& style = ImGui::GetStyle();
  float width = style.ItemSpacing.x * static_cast<float>(labels.size() - 1);
  for (const char* label : labels) {
    width += ImGui::CalcTextSize(label).x + style.FramePadding.x * 2;
  }
  const float room = ImGui::GetContentRegionAvail().x;
  if (room > width) {
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + room - width);
  }

  const bool canGoBack = (step == 2 && !checking) ||
                         (step == 3 && (view.stage == gf::Stage::Ready || view.stage == gf::Stage::Refused));
  ImGui::BeginDisabled(!canGoBack);
  if (ImGui::Button(back)) {
    if (step == 2) {
      window.model.reset();
      endFirmwareHold();
      if (window.ownedConnection && pipeHost_ != nullptr) {
        pipeHost_->driverHost().close(window.driverDeviceId);
      }
      window.ownedConnection = false;
      window.error.clear();
      window.page = FirmwareWindow::Page::ChooseDevice;
    } else {
      window.runPage = false;
    }
  }
  ImGui::EndDisabled();
  ImGui::SameLine();

  const bool canGoNext = (step == 1 && canPickDevice) || (step == 2 && view.stage == gf::Stage::Ready);
  ImGui::BeginDisabled(!canGoNext);
  if (ImGui::Button(next)) {
    if (step == 1) {
      openPicked();
    } else {
      window.runPage = true;
    }
  }
  ImGui::EndDisabled();
  ImGui::SameLine();

  ImGui::BeginDisabled(!(step == 3 && view.stage == gf::Stage::Ready));
  if (ImGui::Button(startUpdate)) {
    window.leavingUpdateMode = false;
    window.model->start();
  }
  ImGui::EndDisabled();
  ImGui::SameLine();

  if (startingProgram) {
    ImGui::BeginDisabled(checking);
    if (ImGui::Button(startProgram)) {
      window.leavingUpdateMode = true;
      window.model->startProgram();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
  }
  if (confirming) {
    if (ImGui::Button(stopAnyway)) {
      window.model->cancel(true);
    }
    ImGui::SameLine();
  }

  const bool busy = checking || view.stage == gf::Stage::Running;
  const bool stopping = step == 3 && view.stage == gf::Stage::Running;
  ImGui::BeginDisabled((busy && !stopping) || (stopping && view.cancelRequested));
  const bool escape = !busy && ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !ImGui::IsAnyItemActive();
  if (ImGui::Button(cancel) || escape) {
    if (stopping) {
      window.model->cancel(false);
    } else {
      close = true;
    }
  }
  ImGui::EndDisabled();

  if (close) {
    ImGui::CloseCurrentPopup();
  }
  ImGui::EndPopup();
}

}
