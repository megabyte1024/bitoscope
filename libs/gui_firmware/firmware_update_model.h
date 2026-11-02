/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

#include "facet_client.h"
#include "firmware_update_pipe_client.h"
#include "ipc_firmware_update.h"
#include "ipc_messages.h"

namespace bitoscope::gui_firmware {

class FirmwareEventBoard {
 public:
  struct Latest {
    std::optional<ipc::firmware::Progress> progress;
    std::optional<ipc::firmware::Finished> finished;
  };

  void onFacetEvent(std::string_view driverDeviceId, const ipc::FacetEvent& event);
  Latest latest(std::string_view driverDeviceId) const;
  void clear(std::string_view driverDeviceId);

 private:
  mutable std::mutex mutex_;
  std::map<std::string, Latest, std::less<>> latest_;
};

enum class Stage { Idle, Working, Ready, Refused, Running, Finished };

struct ModelView {
  Stage stage = Stage::Idle;
  std::string file;
  std::string message;
  std::optional<ipc::firmware::Verdict> verdict;
  std::string fileModel;
  bool inBootloader = false;
  std::string deviceModel;
  std::string chosenModel;
  bool confirmUnknownFile = false;
  bool confirmOtherModel = false;
  std::optional<ipc::firmware::Progress> progress;
  std::optional<ipc::firmware::Finished> finished;
  bool cancelNeedsConfirmation = false;
  bool cancelRequested = false;
};

class FirmwareUpdateModel {
 public:
  FirmwareUpdateModel(host::FacetTransport& transport, std::string driverDeviceId, ipc::firmware::FirmwareUpdateInfo info,
                      FirmwareEventBoard& board,
                      std::chrono::milliseconds status_every = std::chrono::milliseconds(2000));
  ~FirmwareUpdateModel();
  FirmwareUpdateModel(const FirmwareUpdateModel&) = delete;
  FirmwareUpdateModel& operator=(const FirmwareUpdateModel&) = delete;

  ModelView view() const;
  bool busy() const;

  void chooseFile(std::string path);
  void setChosenModel(std::string model);
  void setConfirmUnknownFile(bool confirmed);
  void setConfirmOtherModel(bool confirmed);
  void start();
  void cancel(bool confirm_cannot_capture);
  void startProgram();
  void pollStatus();

 private:
  void post(std::function<void()> job);
  void run(std::stop_token stop);
  void recheck();
  void checkNow();
  void finishRefused(std::string message, std::optional<ipc::firmware::Verdict> verdict);
  std::string modelArgument() const;

  host::FacetTransport& transport_;
  std::string driverDeviceId_;
  ipc::firmware::FirmwareUpdateInfo info_;
  FirmwareEventBoard& board_;
  std::chrono::milliseconds statusEvery_;

  mutable std::mutex mutex_;
  std::condition_variable_any wake_;
  std::deque<std::function<void()>> jobs_;
  ModelView view_;
  std::optional<uint32_t> uploadId_;
  std::optional<ipc::firmware::Finished> statusFinished_;
  std::chrono::steady_clock::time_point lastStatus_{};
  bool statusPending_ = false;
  std::jthread worker_;
};

}
