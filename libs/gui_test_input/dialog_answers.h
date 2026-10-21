/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstddef>
#include <deque>
#include <mutex>
#include <optional>
#include <string>

namespace bitoscope::gui_test_input {

enum class DialogKind { Save, Open };

struct DialogDecision {
  std::optional<std::string> path;
  std::string note;
};

class DialogAnswers {
 public:
  void queueSave(std::string path);
  void queueOpen(std::string path);
  void queueCancel();

  DialogDecision take(DialogKind asked);

  std::size_t pending() const;

 private:
  struct Answer {
    enum class Kind { Save, Open, Cancel };
    Kind kind = Kind::Cancel;
    std::string path;
  };

  void push(Answer answer);

  mutable std::mutex mutex_;
  std::deque<Answer> queue_;
};

}
