/*
 * SPDX-License-Identifier: MIT
 */

#include "panel_capture.h"

#include <utility>

namespace bitoscope::gui_capture {

bool PanelCapture::hasUnsavedCapture() const {
  return kept_.has_value() && (!dataSaved_ || viewUnsaved_ || decodersUnsaved_);
}

void PanelCapture::markViewEdited() {
  if (kept_.has_value()) viewUnsaved_ = true;
}

void PanelCapture::markDecodersEdited() {
  if (kept_.has_value()) decodersUnsaved_ = true;
}

void PanelCapture::release() {
  kept_.reset();
  dataSaved_ = false;
  knownFile_.reset();
  viewUnsaved_ = false;
  decodersUnsaved_ = false;
  fromFile_ = false;
}

std::expected<void, std::string> PanelCapture::trim(TrimSide side, uint64_t at) {
  if (saving_) return std::unexpected(std::string("capture is being saved; trim it after saving ends"));
  if (!kept_) return std::unexpected(std::string("there is no capture to trim"));
  auto trimmed = trimmedOutcome(*kept_, side, at);
  if (!trimmed) return std::unexpected(std::move(trimmed.error()));
  kept_ = std::move(*trimmed);
  dataSaved_ = false;
  knownFile_.reset();
  return {};
}

void PanelCapture::started(uint64_t run) {
  release();
  run_ = run;
  stopRequested_ = false;
  progress_.reset();
  retries_.clear();
  lastError_.reset();
}

bool PanelCapture::requestStop() {
  if (!run_.has_value() || stopRequested_) return false;
  stopRequested_ = true;
  return true;
}

void PanelCapture::progress(uint64_t run, const bitoscope::acquisition::Progress& progress) {
  if (run_ == run) progress_ = progress;
}

void PanelCapture::retrying(uint64_t run, const bitoscope::acquisition::AcquireRetry& retry) {
  if (run_ == run) retries_.push_back(retry);
}

std::optional<PanelCapture::SaveBlocked> PanelCapture::saveBlocked() const {
  if (run_.has_value()) return SaveBlocked::Running;
  if (!kept_.has_value()) return SaveBlocked::NoCapture;
  if (saving_) return SaveBlocked::Saving;
  return std::nullopt;
}

bool PanelCapture::beginSave() {
  if (saveBlocked().has_value()) return false;
  saving_ = true;
  lastSaveError_.reset();
  return true;
}

void PanelCapture::fullSaveFinished(KnownFile file, const std::expected<void, std::string>& result) {
  if (!saving_) return;
  saving_ = false;
  if (result.has_value()) {
    dataSaved_ = true;
    knownFile_ = std::move(file);
    viewUnsaved_ = false;
    decodersUnsaved_ = false;
    lastSaveError_.reset();
  } else {
    lastSaveError_ = result.error();
  }
}

void PanelCapture::viewSaveFinished(const std::expected<void, std::string>& result) {
  if (!saving_) return;
  saving_ = false;
  if (result.has_value()) {
    viewUnsaved_ = false;
    decodersUnsaved_ = false;
    lastSaveError_.reset();
  } else {
    lastSaveError_ = result.error();
  }
}

void PanelCapture::loaded(CaptureOutcome outcome, KnownFile file) {
  kept_ = std::move(outcome);
  dataSaved_ = true;
  knownFile_ = std::move(file);
  viewUnsaved_ = false;
  decodersUnsaved_ = false;
  fromFile_ = true;
  run_.reset();
  stopRequested_ = false;
  lastError_.reset();
}

void PanelCapture::finished(uint64_t run, CaptureOutcome outcome) {
  if (run_ != run) return;
  run_.reset();
  stopRequested_ = false;
  if (outcome.kept()) {
    kept_ = std::move(outcome);
    dataSaved_ = false;
    knownFile_.reset();
    viewUnsaved_ = false;
    decodersUnsaved_ = false;
    lastError_.reset();
  } else {
    kept_.reset();
    lastError_ = std::move(outcome.message);
  }
}

}
