/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <expected>
#include <optional>
#include <string>

#include "acquisition.h"
#include "unsaved_policy.h"

namespace bitoscope::gui_capture {

enum class UnsavedChoice { Save, DontSave, Cancel };

class StartFlow {
 public:
  enum class Step {
    Idle,
    AskUnsaved,
    CheckMemory,
    AskStartAnyway,
    ShowRefusal,
    Start,
  };

  Step step() const { return step_; }

  void press(bool hasUnsavedCapture, UnsavedPolicy policy);

  void answerUnsaved(UnsavedChoice choice, bool remember);

  void memoryChecked(const std::expected<bitoscope::acquisition::MemoryCheck, std::string>& check);

  void answerStartAnyway(bool start);

  void dismiss();

  void reset();

  std::optional<uint64_t> floor() const { return floor_; }
  const std::string& message() const { return message_; }

  std::optional<UnsavedPolicy> takeRememberedPolicy();

 private:
  Step step_ = Step::Idle;
  std::optional<uint64_t> floor_;
  std::string message_;
  std::optional<UnsavedPolicy> remembered_;
};

}
