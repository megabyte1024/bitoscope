/*
 * SPDX-License-Identifier: MIT
 */

#include "firmware_update_model.h"

#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <utility>
#include <variant>
#include <vector>

namespace bitoscope::gui_firmware {

namespace {

namespace fw = bitoscope::ipc::firmware;

host::pipe::FirmwareUpdateClient clientOf(host::FacetTransport& transport, const std::string& id,
                                          const fw::FirmwareUpdateInfo& info) {
  return host::pipe::FirmwareUpdateClient(
      host::FacetClient(transport, id, std::string(host::pipe::kFirmwareUpdateFacetName)), info);
}

}

void FirmwareEventBoard::onFacetEvent(std::string_view driverDeviceId, const ipc::FacetEvent& event) {
  if (event.facet != fw::kFacetName) return;
  const auto decoded = fw::decodeEvent(event.payload);
  if (!decoded) return;
  std::lock_guard lock(mutex_);
  Latest& latest = latest_[std::string(driverDeviceId)];
  if (const auto* progress = std::get_if<fw::Progress>(&*decoded)) {
    latest.progress = *progress;
  } else {
    latest.finished = std::get<fw::Finished>(*decoded);
  }
}

FirmwareEventBoard::Latest FirmwareEventBoard::latest(std::string_view driverDeviceId) const {
  std::lock_guard lock(mutex_);
  const auto found = latest_.find(driverDeviceId);
  return found == latest_.end() ? Latest{} : found->second;
}

void FirmwareEventBoard::clear(std::string_view driverDeviceId) {
  std::lock_guard lock(mutex_);
  if (const auto found = latest_.find(driverDeviceId); found != latest_.end()) latest_.erase(found);
}

FirmwareUpdateModel::FirmwareUpdateModel(host::FacetTransport& transport, std::string driverDeviceId,
                                         fw::FirmwareUpdateInfo info, FirmwareEventBoard& board,
                                         std::chrono::milliseconds status_every)
    : transport_(transport),
      driverDeviceId_(std::move(driverDeviceId)),
      info_(std::move(info)),
      board_(board),
      statusEvery_(status_every) {
  view_.inBootloader = info_.running == fw::Running::bootloader;
  view_.deviceModel = info_.model;
  worker_ = std::jthread([this](std::stop_token stop) { run(stop); });
}

FirmwareUpdateModel::~FirmwareUpdateModel() = default;

void FirmwareUpdateModel::post(std::function<void()> job) {
  {
    std::lock_guard lock(mutex_);
    jobs_.push_back(std::move(job));
  }
  wake_.notify_one();
}

void FirmwareUpdateModel::run(std::stop_token stop) {
  for (;;) {
    std::function<void()> job;
    {
      std::unique_lock lock(mutex_);
      if (!wake_.wait(lock, stop, [this] { return !jobs_.empty(); })) return;
      job = std::move(jobs_.front());
      jobs_.pop_front();
    }
    job();
  }
}

ModelView FirmwareUpdateModel::view() const {
  std::lock_guard lock(mutex_);
  ModelView copy = view_;
  if (copy.stage == Stage::Running) {
    const FirmwareEventBoard::Latest latest = board_.latest(driverDeviceId_);
    if (latest.progress) copy.progress = latest.progress;
    const std::optional<fw::Finished> done = latest.finished ? latest.finished : statusFinished_;
    if (done) {
      copy.finished = done;
      copy.stage = Stage::Finished;
    }
  }
  return copy;
}

bool FirmwareUpdateModel::busy() const {
  const Stage stage = view().stage;
  return stage == Stage::Working || stage == Stage::Running;
}

std::string FirmwareUpdateModel::modelArgument() const {
  if (!view_.inBootloader || view_.chosenModel.empty()) return {};
  return "DL" + view_.chosenModel;
}

void FirmwareUpdateModel::finishRefused(std::string message, std::optional<fw::Verdict> verdict) {
  std::lock_guard lock(mutex_);
  view_.stage = Stage::Refused;
  view_.message = std::move(message);
  view_.verdict = verdict;
}

void FirmwareUpdateModel::chooseFile(std::string path) {
  {
    std::lock_guard lock(mutex_);
    if (view_.stage == Stage::Working || view_.stage == Stage::Running) return;
    view_.stage = Stage::Working;
    view_.file = path;
    view_.message = "Checking file...";
    view_.verdict.reset();
    view_.fileModel.clear();
    view_.progress.reset();
    view_.finished.reset();
    view_.cancelNeedsConfirmation = false;
    view_.cancelRequested = false;
    uploadId_.reset();
  }
  post([this, path = std::move(path)] {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
      finishRefused(std::format("Cannot read file '{}'", path), std::nullopt);
      return;
    }
    const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (bytes.empty()) {
      finishRefused(std::format("File '{}' is empty", path), std::nullopt);
      return;
    }
    auto client = clientOf(transport_, driverDeviceId_, info_);
    const auto id = client.upload(bytes, std::filesystem::path(path).filename().string());
    if (!id) {
      finishRefused(id.error(), std::nullopt);
      return;
    }
    {
      std::lock_guard lock(mutex_);
      uploadId_ = *id;
    }
    checkNow();
  });
}

void FirmwareUpdateModel::checkNow() {
  uint32_t id = 0;
  std::string model;
  host::pipe::FirmwareConfirmations confirmed;
  {
    std::lock_guard lock(mutex_);
    if (!uploadId_) return;
    id = *uploadId_;
    model = modelArgument();
    confirmed = {view_.confirmUnknownFile, view_.confirmOtherModel};
  }
  auto client = clientOf(transport_, driverDeviceId_, info_);
  const auto reply = client.check(id, model, confirmed);
  std::lock_guard lock(mutex_);
  if (!reply) {
    view_.stage = Stage::Refused;
    view_.message = reply.error();
    view_.verdict.reset();
    return;
  }
  view_.verdict = reply->verdict;
  view_.fileModel = reply->fileModel;
  view_.message = reply->message;
  view_.stage = reply->verdict == fw::Verdict::fits ? Stage::Ready : Stage::Refused;
}

void FirmwareUpdateModel::recheck() {
  {
    std::lock_guard lock(mutex_);
    if (!uploadId_ || (view_.stage != Stage::Ready && view_.stage != Stage::Refused)) return;
    view_.stage = Stage::Working;
    view_.message = "Checking file...";
  }
  post([this] { checkNow(); });
}

void FirmwareUpdateModel::setChosenModel(std::string model) {
  {
    std::lock_guard lock(mutex_);
    view_.chosenModel = std::move(model);
  }
  recheck();
}

void FirmwareUpdateModel::setConfirmUnknownFile(bool confirmed) {
  {
    std::lock_guard lock(mutex_);
    view_.confirmUnknownFile = confirmed;
  }
  recheck();
}

void FirmwareUpdateModel::setConfirmOtherModel(bool confirmed) {
  {
    std::lock_guard lock(mutex_);
    view_.confirmOtherModel = confirmed;
  }
  recheck();
}

void FirmwareUpdateModel::start() {
  uint32_t id = 0;
  std::string model;
  host::pipe::FirmwareConfirmations confirmed;
  {
    std::lock_guard lock(mutex_);
    if (view_.stage != Stage::Ready || !uploadId_) return;
    id = *uploadId_;
    model = modelArgument();
    confirmed = {view_.confirmUnknownFile, view_.confirmOtherModel};
    view_.stage = Stage::Running;
    view_.message = "Starting update...";
    view_.progress.reset();
    view_.finished.reset();
    view_.cancelNeedsConfirmation = false;
    view_.cancelRequested = false;
    statusFinished_.reset();
    statusPending_ = false;
    lastStatus_ = std::chrono::steady_clock::now();
  }
  board_.clear(driverDeviceId_);
  post([this, id, model = std::move(model), confirmed] {
    auto client = clientOf(transport_, driverDeviceId_, info_);
    if (const auto started = client.start(id, model, confirmed); !started) finishRefused(started.error(), std::nullopt);
  });
}

void FirmwareUpdateModel::startProgram() {
  {
    std::lock_guard lock(mutex_);
    if (view_.stage == Stage::Working || view_.stage == Stage::Running) return;
    view_.stage = Stage::Running;
    view_.message = "Restarting analyzer...";
    view_.progress.reset();
    view_.finished.reset();
    view_.cancelNeedsConfirmation = false;
    view_.cancelRequested = false;
    statusFinished_.reset();
    statusPending_ = false;
    lastStatus_ = std::chrono::steady_clock::now();
  }
  board_.clear(driverDeviceId_);
  post([this] {
    auto client = clientOf(transport_, driverDeviceId_, info_);
    if (const auto started = client.startProgram(); !started) finishRefused(started.error(), std::nullopt);
  });
}

void FirmwareUpdateModel::cancel(bool confirm_cannot_capture) {
  {
    std::lock_guard lock(mutex_);
    if (view_.stage != Stage::Running) return;
  }
  post([this, confirm_cannot_capture] {
    auto client = clientOf(transport_, driverDeviceId_, info_);
    const auto answer = client.cancel(confirm_cannot_capture);
    std::lock_guard lock(mutex_);
    if (!answer) {
      view_.message = answer.error();
    } else if (*answer == fw::CancelAnswer::accepted) {
      view_.cancelRequested = true;
      view_.cancelNeedsConfirmation = false;
      view_.message = "Stopping update...";
    } else if (*answer == fw::CancelAnswer::needsConfirmation) {
      view_.cancelNeedsConfirmation = true;
    } else {
      view_.message = "Update is almost done and can no longer be stopped.";
    }
  });
}

void FirmwareUpdateModel::pollStatus() {
  {
    std::lock_guard lock(mutex_);
    const auto now = std::chrono::steady_clock::now();
    if (view_.stage != Stage::Running || statusPending_ || statusFinished_ ||
        now - lastStatus_ < statusEvery_ || board_.latest(driverDeviceId_).finished) {
      return;
    }
    statusPending_ = true;
    lastStatus_ = now;
  }
  post([this] {
    auto client = clientOf(transport_, driverDeviceId_, info_);
    const auto status = client.status();
    std::lock_guard lock(mutex_);
    statusPending_ = false;
    if (status && !status->running && status->last && std::holds_alternative<fw::Finished>(*status->last)) {
      statusFinished_ = std::get<fw::Finished>(*status->last);
    }
  });
}

}
