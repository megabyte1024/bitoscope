/*
 * SPDX-License-Identifier: MIT
 */

#include "start_flow.h"

#include <utility>

namespace bitoscope::gui_capture {

void StartFlow::press(bool hasUnsavedCapture, UnsavedPolicy policy) {
  if (step_ != Step::Idle) return;
  floor_.reset();
  message_.clear();
  step_ = hasUnsavedCapture && policy == UnsavedPolicy::Ask ? Step::AskUnsaved : Step::CheckMemory;
}

void StartFlow::answerUnsaved(UnsavedChoice choice, bool remember) {
  if (step_ != Step::AskUnsaved) return;
  switch (choice) {
    case UnsavedChoice::Cancel:
      step_ = Step::Idle;
      return;
    case UnsavedChoice::DontSave:
      if (remember) remembered_ = UnsavedPolicy::DontSave;
      step_ = Step::CheckMemory;
      return;
    case UnsavedChoice::Save:
      step_ = Step::CheckMemory;
      return;
  }
}

void StartFlow::memoryChecked(const std::expected<bitoscope::acquisition::MemoryCheck, std::string>& check) {
  if (step_ != Step::CheckMemory) return;
  if (!check.has_value()) {
    message_ = check.error();
    step_ = Step::ShowRefusal;
    return;
  }
  if (check->answer == bitoscope::acquisition::MemoryCheck::Answer::Warn) {
    floor_ = check->floor_samples;
    message_ = check->text;
    step_ = Step::AskStartAnyway;
    return;
  }
  step_ = Step::Start;
}

void StartFlow::answerStartAnyway(bool start) {
  if (step_ != Step::AskStartAnyway) return;
  step_ = start ? Step::Start : Step::Idle;
}

void StartFlow::dismiss() {
  if (step_ == Step::ShowRefusal) step_ = Step::Idle;
}

void StartFlow::reset() {
  step_ = Step::Idle;
  floor_.reset();
  message_.clear();
}

std::optional<UnsavedPolicy> StartFlow::takeRememberedPolicy() { return std::exchange(remembered_, std::nullopt); }

}
