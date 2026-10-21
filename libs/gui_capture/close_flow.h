/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "unsaved_policy.h"

namespace bitoscope::gui_capture {

struct ClosingPanel {
  uint64_t id = 0;
  std::string name;
  bool unsaved = false;
  bool saving = false;
  bool running = false;
};

enum class CloseAnswer { Save, DontSave, Cancel };

class CloseFlow {
 public:
  struct Action {
    enum class Kind {
      None,
      Ask,
      StartSave,
      Wait,
      ClosePanel,
      CloseApp,
      Abort,
    };
    Kind kind = Kind::None;
    uint64_t panel = 0;
    bool operator==(const Action&) const = default;
  };

  bool requestPanelClose(uint64_t panel, UnsavedPolicy remembered = UnsavedPolicy::Ask);
  bool requestAppClose(UnsavedPolicy remembered = UnsavedPolicy::Ask);

  void answer(CloseAnswer answer);
  void saveFinished(uint64_t panel, bool succeeded);

  Action next(const std::vector<ClosingPanel>& panels);

  bool active() const { return scope_ != Scope::None; }
  bool closingApp() const { return scope_ == Scope::App; }
  std::optional<uint64_t> question() const { return question_; }
  bool savingQuestion() const { return awaitingSave_; }

 private:
  enum class Scope { None, Panel, App };
  Action finish(Action action);

  Scope scope_ = Scope::None;
  uint64_t panel_ = 0;
  UnsavedPolicy remembered_ = UnsavedPolicy::Ask;
  std::optional<uint64_t> question_;
  bool awaitingSave_ = false;
  bool saveStartPending_ = false;
  std::optional<bool> saveResult_;
  bool cancelled_ = false;
  std::set<uint64_t> answered_;
};

}
