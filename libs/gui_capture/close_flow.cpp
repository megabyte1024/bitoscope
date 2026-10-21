/*
 * SPDX-License-Identifier: MIT
 */

#include "close_flow.h"

namespace bitoscope::gui_capture {

namespace {

const ClosingPanel* findPanel(const std::vector<ClosingPanel>& panels, uint64_t id) {
  for (const ClosingPanel& panel : panels) {
    if (panel.id == id) return &panel;
  }
  return nullptr;
}

}

bool CloseFlow::requestPanelClose(uint64_t panel, UnsavedPolicy remembered) {
  if (active()) return false;
  finish(Action{});
  scope_ = Scope::Panel;
  panel_ = panel;
  remembered_ = remembered;
  return true;
}

bool CloseFlow::requestAppClose(UnsavedPolicy remembered) {
  if (scope_ == Scope::App) return true;
  if (scope_ == Scope::Panel) return false;
  finish(Action{});
  scope_ = Scope::App;
  remembered_ = remembered;
  return true;
}

void CloseFlow::answer(CloseAnswer answer) {
  if (!active() || !question_ || awaitingSave_ || saveStartPending_) return;
  switch (answer) {
  case CloseAnswer::Save:
    saveStartPending_ = true;
    break;
  case CloseAnswer::DontSave:
    answered_.insert(*question_);
    question_.reset();
    break;
  case CloseAnswer::Cancel:
    cancelled_ = true;
    break;
  }
}

void CloseFlow::saveFinished(uint64_t panel, bool succeeded) {
  if (awaitingSave_ && question_ == panel) saveResult_ = succeeded;
}

CloseFlow::Action CloseFlow::finish(Action action) {
  scope_ = Scope::None;
  panel_ = 0;
  question_.reset();
  awaitingSave_ = false;
  saveStartPending_ = false;
  saveResult_.reset();
  cancelled_ = false;
  answered_.clear();
  return action;
}

CloseFlow::Action CloseFlow::next(const std::vector<ClosingPanel>& panels) {
  if (scope_ == Scope::None) return Action{};
  if (cancelled_) return finish(Action{Action::Kind::Abort, 0});

  if (saveStartPending_) {
    saveStartPending_ = false;
    awaitingSave_ = true;
    return Action{Action::Kind::StartSave, *question_};
  }
  if (awaitingSave_) {
    const uint64_t id = *question_;
    if (saveResult_.has_value()) {
      if (!*saveResult_) return finish(Action{Action::Kind::Abort, 0});
      answered_.insert(id);
    } else if (const ClosingPanel* panel = findPanel(panels, id); panel != nullptr) {
      if (panel->saving) return Action{Action::Kind::Wait, 0};
      return finish(Action{Action::Kind::Abort, 0});
    }
    awaitingSave_ = false;
    saveResult_.reset();
    question_.reset();
  }

  if (scope_ == Scope::Panel) {
    const ClosingPanel* panel = findPanel(panels, panel_);
    if (panel == nullptr) return finish(Action{});
    if (panel->running) return finish(Action{Action::Kind::Abort, 0});
    if (panel->saving) return Action{Action::Kind::Wait, 0};
    if (panel->unsaved && !answered_.contains(panel_) && remembered_ == UnsavedPolicy::Ask) {
      question_ = panel_;
      return Action{Action::Kind::Ask, panel_};
    }
    return finish(Action{Action::Kind::ClosePanel, panel_});
  }

  if (question_) {
    const ClosingPanel* panel = findPanel(panels, *question_);
    if (panel != nullptr && panel->unsaved && !panel->saving && !panel->running) {
      return Action{Action::Kind::Ask, *question_};
    }
    question_.reset();
  }
  for (const ClosingPanel& panel : panels) {
    if (answered_.contains(panel.id) || panel.running) continue;
    if (panel.saving) return Action{Action::Kind::Wait, 0};
    if (panel.unsaved && remembered_ == UnsavedPolicy::Ask) {
      question_ = panel.id;
      return Action{Action::Kind::Ask, panel.id};
    }
  }
  return finish(Action{Action::Kind::CloseApp, 0});
}

}
