/*
 * SPDX-License-Identifier: MIT
 */

#include "sequencer.h"

#include <array>
#include <cstdlib>
#include <format>
#include <utility>

namespace bitoscope::gui_test_input {

namespace {

using Events = std::vector<InputEvent>;

InputEvent mousePos(PointPx physical, CoordinateScale scale) {
  const LogicalPoint logical = toLogical(physical, scale);
  InputEvent e;
  e.kind = InputEvent::Kind::MousePos;
  e.x = logical.x;
  e.y = logical.y;
  return e;
}

InputEvent mouseButton(MouseButtonId button, bool down) {
  InputEvent e;
  e.kind = InputEvent::Kind::MouseButton;
  e.button = button;
  e.down = down;
  return e;
}

InputEvent keyEvent(KeyId key, bool down) {
  InputEvent e;
  e.kind = InputEvent::Kind::Key;
  e.key = key;
  e.down = down;
  return e;
}

std::vector<KeyId> modifiersOf(const KeyChord& chord) {
  std::vector<KeyId> mods;
  if (chord.ctrl) {
    mods.push_back(KeyId::ModCtrl);
  }
  if (chord.shift) {
    mods.push_back(KeyId::ModShift);
  }
  if (chord.alt) {
    mods.push_back(KeyId::ModAlt);
  }
  if (chord.super) {
    mods.push_back(KeyId::ModSuper);
  }
  return mods;
}

Events chordDown(const KeyChord& chord) {
  Events events;
  for (const KeyId mod : modifiersOf(chord)) {
    events.push_back(keyEvent(mod, true));
  }
  events.push_back(keyEvent(chord.key, true));
  return events;
}

Events chordUp(const KeyChord& chord) {
  Events events;
  events.push_back(keyEvent(chord.key, false));
  const std::vector<KeyId> mods = modifiersOf(chord);
  for (auto it = mods.rbegin(); it != mods.rend(); ++it) {
    events.push_back(keyEvent(*it, false));
  }
  return events;
}

std::int64_t scaledOffset(std::int64_t delta, std::int64_t i, std::int64_t steps) {
  const std::int64_t numerator = delta * i;
  const std::int64_t magnitude = (std::llabs(numerator) * 2 + steps) / (2 * steps);
  return numerator < 0 ? -magnitude : magnitude;
}

}

std::vector<Sequencer::Step> Sequencer::expand(const Command& command, CoordinateScale scale) {
  std::vector<Step> steps;
  const auto frame = [&steps](Events events) {
    Step step;
    step.events = std::move(events);
    steps.push_back(std::move(step));
  };
  const auto queryFrame = [&steps](FrameQuery::Kind kind, std::string path = {}, std::uint32_t scale = 1) {
    FrameQuery query;
    query.kind = kind;
    query.path = std::move(path);
    query.scale = scale;
    Step step;
    step.query = std::move(query);
    steps.push_back(std::move(step));
  };

  // WHY: Named before the struct: GCC rejects a member named like the entity its own type refers to.
  using FrameFn = decltype(frame);
  using QueryFrameFn = decltype(queryFrame);

  struct Visitor {
    CoordinateScale scale;
    FrameFn& frame;
    QueryFrameFn& queryFrame;
    std::vector<Step>& steps;

    void operator()(const command::Move& c) const { frame({mousePos(c.at, scale)}); }
    void operator()(const command::Down& c) const { frame({mouseButton(c.button, true)}); }
    void operator()(const command::Up& c) const { frame({mouseButton(c.button, false)}); }
    void operator()(const command::Click& c) const {
      frame({mousePos(c.at, scale)});
      frame({mouseButton(c.button, true)});
      frame({mouseButton(c.button, false)});
    }
    void operator()(const command::DoubleClick& c) const {
      frame({mousePos(c.at, scale)});
      for (int i = 0; i < 2; ++i) {
        frame({mouseButton(MouseButtonId::Left, true)});
        frame({mouseButton(MouseButtonId::Left, false)});
      }
    }
    void operator()(const command::Drag& c) const {
      frame({mousePos(c.from, scale)});
      frame({mouseButton(MouseButtonId::Left, true)});
      const std::int64_t n = c.steps;
      for (std::int64_t i = 1; i <= n; ++i) {
        const PointPx at{static_cast<std::int32_t>(c.from.x + scaledOffset(c.to.x - c.from.x, i, n)),
                         static_cast<std::int32_t>(c.from.y + scaledOffset(c.to.y - c.from.y, i, n))};
        frame({mousePos(at, scale)});
      }
      frame({mouseButton(MouseButtonId::Left, false)});
    }
    void operator()(const command::Wheel& c) const {
      InputEvent e;
      e.kind = InputEvent::Kind::MouseWheel;
      e.x = static_cast<float>(c.dx);
      e.y = static_cast<float>(c.dy);
      frame({e});
    }
    void operator()(const command::KeyPress& c) const {
      frame(chordDown(c.chord));
      frame(chordUp(c.chord));
    }
    void operator()(const command::KeyDown& c) const { frame(chordDown(c.chord)); }
    void operator()(const command::KeyUp& c) const { frame(chordUp(c.chord)); }
    void operator()(const command::Text& c) const {
      InputEvent e;
      e.kind = InputEvent::Kind::Text;
      e.text = c.text;
      frame({e});
    }
    void operator()(const command::Frames& c) const {
      for (std::uint32_t i = 0; i < c.count; ++i) {
        frame({});
      }
    }
    void operator()(const command::Wait&) const {}
    void operator()(const command::Screenshot& c) const {
      queryFrame(FrameQuery::Kind::Screenshot, c.path, c.scale);
    }
    void operator()(const command::Windows&) const { queryFrame(FrameQuery::Kind::Windows); }
    void operator()(const command::Hover&) const { queryFrame(FrameQuery::Kind::Hover); }
    void operator()(const command::Info&) const { queryFrame(FrameQuery::Kind::Info); }
    void operator()(const command::Quit&) const {
      Step step;
      step.quit = true;
      steps.push_back(std::move(step));
    }
    void operator()(const command::Close&) const {
      Step step;
      step.close = true;
      steps.push_back(std::move(step));
    }
    void operator()(const command::DialogSave& c) const { queryFrame(FrameQuery::Kind::DialogSave, c.path); }
    void operator()(const command::DialogOpen& c) const { queryFrame(FrameQuery::Kind::DialogOpen, c.path); }
    void operator()(const command::DialogCancel&) const { queryFrame(FrameQuery::Kind::DialogCancel); }
    void operator()(const command::Capture&) const { queryFrame(FrameQuery::Kind::Capture); }
    void operator()(const command::WaitUntil&) const {}
    void operator()(const command::FrameMark&) const { queryFrame(FrameQuery::Kind::FrameMark); }
    void operator()(const command::FrameTimesQuery& c) const {
      queryFrame(FrameQuery::Kind::FrameTimes);
      steps.back().query->selection = c.selection;
    }
    void operator()(const command::PresentMode& c) const {
      queryFrame(FrameQuery::Kind::PresentMode);
      steps.back().query->presentMode = c.mode;
    }
  };
  std::visit(Visitor{scale, frame, queryFrame, steps}, command);
  return steps;
}

void Sequencer::push(std::uint64_t ticket, Command command) {
  queue_.push_back(Queued{ticket, std::move(command)});
}

bool Sequencer::idle() const { return !running_.has_value() && queue_.empty(); }

FramePlan Sequencer::beginFrame(std::uint64_t nowMs, CoordinateScale scale) {
  FramePlan plan;
  if (!running_) {
    if (queue_.empty()) {
      return plan;
    }
    Queued next = std::move(queue_.front());
    queue_.pop_front();
    Running running;
    running.ticket = next.ticket;
    running.steps = expand(next.command, scale);
    running.waitOnly = std::holds_alternative<command::Wait>(next.command) ||
                       std::holds_alternative<command::WaitUntil>(next.command);
    running.command = std::move(next.command);
    running_ = std::move(running);
  }
  Running& run = *running_;
  ++run.frames;
  if (run.waitOnly) {
    if (!run.startMs) {
      run.startMs = nowMs;
    }
    return plan;
  }
  if (run.next < run.steps.size()) {
    Step& step = run.steps[run.next++];
    plan.events = std::move(step.events);
    plan.query = std::move(step.query);
    plan.quit = step.quit;
    plan.close = step.close;
    run.queryPending = plan.query.has_value();
  }
  return plan;
}

std::vector<Reply> Sequencer::endFrame(std::uint64_t nowMs, std::optional<std::string> queryReply,
                                       const Observation& observation) {
  std::vector<Reply> replies;
  if (!running_) {
    return replies;
  }
  Running& run = *running_;
  if (run.waitOnly) {
    if (const auto* until = std::get_if<command::WaitUntil>(&run.command)) {
      // WHY: Check the condition first: one that holds in the last allowed frame is not a timeout.
      if (conditionHolds(until->condition, observation)) {
        replies.push_back(Reply{run.ticket, std::format("ok frames={}", run.frames)});
        running_.reset();
      } else if (run.startMs && nowMs - *run.startMs >= until->timeoutMs) {
        replies.push_back(Reply{run.ticket, std::format("error wait-until: timed out after {} ms waiting for {}",
                                                        until->timeoutMs,
                                                        describeCondition(until->condition, observation))});
        running_.reset();
      }
      return replies;
    }
    const auto& wait = std::get<command::Wait>(run.command);
    if (run.startMs && nowMs - *run.startMs >= wait.ms) {
      replies.push_back(Reply{run.ticket, std::format("ok frames={}", run.frames)});
      running_.reset();
    }
    return replies;
  }
  if (run.next < run.steps.size()) {
    return replies;
  }
  if (run.queryPending) {
    replies.push_back(Reply{run.ticket, queryReply ? std::move(*queryReply)
                                                   : std::string("error internal: the platform gave no answer")});
  } else if (std::holds_alternative<command::Quit>(run.command) ||
             std::holds_alternative<command::Close>(run.command)) {
    replies.push_back(Reply{run.ticket, "ok"});
  } else {
    replies.push_back(Reply{run.ticket, std::format("ok frames={}", run.frames)});
  }
  running_.reset();
  return replies;
}

}
