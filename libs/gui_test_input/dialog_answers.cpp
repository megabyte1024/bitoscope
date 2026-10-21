/*
 * SPDX-License-Identifier: MIT
 */

#include "dialog_answers.h"

#include <format>
#include <utility>

namespace bitoscope::gui_test_input {

namespace {

const char* kindName(DialogKind kind) { return kind == DialogKind::Save ? "Save" : "Open"; }

}

void DialogAnswers::push(Answer answer) {
  std::lock_guard lock(mutex_);
  queue_.push_back(std::move(answer));
}

void DialogAnswers::queueSave(std::string path) { push(Answer{Answer::Kind::Save, std::move(path)}); }

void DialogAnswers::queueOpen(std::string path) { push(Answer{Answer::Kind::Open, std::move(path)}); }

void DialogAnswers::queueCancel() { push(Answer{Answer::Kind::Cancel, {}}); }

DialogDecision DialogAnswers::take(DialogKind asked) {
  Answer answer;
  {
    std::lock_guard lock(mutex_);
    if (queue_.empty()) {
      return DialogDecision{std::nullopt,
                            std::format("no dialog answer was queued: {} dialog is cancelled", kindName(asked))};
    }
    answer = std::move(queue_.front());
    queue_.pop_front();
  }
  switch (answer.kind) {
  case Answer::Kind::Cancel:
    return DialogDecision{std::nullopt, std::format("dialog-cancel cancelled {} dialog", kindName(asked))};
  case Answer::Kind::Save:
    if (asked == DialogKind::Save) {
      return DialogDecision{answer.path, std::format("dialog-save answered Save dialog: {}", answer.path)};
    }
    return DialogDecision{std::nullopt,
                          std::format("dialog-save answer was queued but Open dialog was asked: cancelled, "
                                      "answer is discarded ({})",
                                      answer.path)};
  case Answer::Kind::Open:
    if (asked == DialogKind::Open) {
      return DialogDecision{answer.path, std::format("dialog-open answered Open dialog: {}", answer.path)};
    }
    return DialogDecision{std::nullopt,
                          std::format("dialog-open answer was queued but Save dialog was asked: cancelled, "
                                      "answer is discarded ({})",
                                      answer.path)};
  }
  return DialogDecision{std::nullopt, "internal: unknown dialog answer"};
}

std::size_t DialogAnswers::pending() const {
  std::lock_guard lock(mutex_);
  return queue_.size();
}

}
