/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <vector>

#include "capture_outcome.h"
#include "capture_trim.h"
#include "progress.h"

namespace bitoscope::gui_capture {

class PanelCapture {
 public:
  const std::optional<CaptureOutcome>& kept() const { return kept_; }
  bool hasUnsavedCapture() const;
  bool viewUnsaved() const { return viewUnsaved_; }
  void markViewEdited();
  bool decodersUnsaved() const { return decodersUnsaved_; }
  void markDecodersEdited();
  void release();
  std::expected<void, std::string> trim(TrimSide side, uint64_t at);

  void started(uint64_t run);
  bool running() const { return run_.has_value(); }
  std::optional<uint64_t> run() const { return run_; }

  bool requestStop();
  bool stopRequested() const { return stopRequested_; }

  void progress(uint64_t run, const bitoscope::acquisition::Progress& progress);
  const std::optional<bitoscope::acquisition::Progress>& lastProgress() const { return progress_; }
  void retrying(uint64_t run, const bitoscope::acquisition::AcquireRetry& retry);
  const std::vector<bitoscope::acquisition::AcquireRetry>& retries() const { return retries_; }

  void finished(uint64_t run, CaptureOutcome outcome);

  const std::optional<std::string>& lastError() const { return lastError_; }

  enum class SaveBlocked { NoCapture, Running, Saving };
  std::optional<SaveBlocked> saveBlocked() const;

  struct KnownFile {
    std::string path;
    uint32_t deviceIndex = 0;
    bool operator==(const KnownFile&) const = default;
  };
  const std::optional<KnownFile>& knownFile() const { return knownFile_; }

  enum class SaveKind { Full, ViewOnly };
  SaveKind saveKind() const { return knownFile_.has_value() ? SaveKind::ViewOnly : SaveKind::Full; }

  bool beginSave();
  bool saving() const { return saving_; }
  void fullSaveFinished(KnownFile file, const std::expected<void, std::string>& result);
  void viewSaveFinished(const std::expected<void, std::string>& result);
  const std::optional<std::string>& lastSaveError() const { return lastSaveError_; }

  void loaded(CaptureOutcome outcome, KnownFile file);
  bool fromFile() const { return fromFile_; }

 private:
  std::optional<CaptureOutcome> kept_;
  bool dataSaved_ = false;
  std::optional<KnownFile> knownFile_;
  bool viewUnsaved_ = false;
  bool decodersUnsaved_ = false;
  bool saving_ = false;
  bool fromFile_ = false;
  std::optional<std::string> lastSaveError_;
  std::optional<uint64_t> run_;
  bool stopRequested_ = false;
  std::optional<bitoscope::acquisition::Progress> progress_;
  std::vector<bitoscope::acquisition::AcquireRetry> retries_;
  std::optional<std::string> lastError_;
};

}
