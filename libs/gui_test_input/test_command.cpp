/*
 * SPDX-License-Identifier: MIT
 */

#include "test_command.h"

#include <charconv>
#include <cstddef>
#include <format>
#include <vector>

#include "key_chord.h"

namespace bitoscope::gui_test_input {

namespace {

bool isSpace(char c) { return c == ' ' || c == '\t'; }

std::string lower(std::string_view text) {
  std::string out(text);
  for (char& c : out) {
    if (c >= 'A' && c <= 'Z') {
      c = static_cast<char>(c - 'A' + 'a');
    }
  }
  return out;
}

std::string_view trim(std::string_view text) {
  while (!text.empty() && (isSpace(text.front()) || text.front() == '\r' || text.front() == '\n')) {
    text.remove_prefix(1);
  }
  while (!text.empty() && (isSpace(text.back()) || text.back() == '\r' || text.back() == '\n')) {
    text.remove_suffix(1);
  }
  return text;
}

std::vector<std::string_view> splitWords(std::string_view text) {
  std::vector<std::string_view> words;
  std::size_t i = 0;
  while (i < text.size()) {
    while (i < text.size() && isSpace(text[i])) {
      ++i;
    }
    const std::size_t begin = i;
    while (i < text.size() && !isSpace(text[i])) {
      ++i;
    }
    if (i > begin) {
      words.push_back(text.substr(begin, i - begin));
    }
  }
  return words;
}

std::string plural(std::size_t count) {
  return std::format("{} argument{}", count, count == 1 ? "" : "s");
}

std::expected<std::int64_t, std::string> parseInteger(std::string_view command, std::string_view token,
                                                     std::int64_t low, std::int64_t high,
                                                     std::string_view what = {}) {
  std::int64_t value = 0;
  const char* const begin = token.data();
  const char* const end = token.data() + token.size();
  const auto [ptr, ec] = std::from_chars(begin, end, value);
  if (ec == std::errc::result_out_of_range) {
    return std::unexpected(std::format("{}: '{}' is too large an integer", command, token));
  }
  if (ec != std::errc{} || ptr != end) {
    return std::unexpected(std::format("{}: '{}' is not an integer", command, token));
  }
  if (value < low || value > high) {
    return std::unexpected(std::format("{}: {}{}{} is outside {}..{}", command, what, what.empty() ? "" : " ",
                                       value, low, high));
  }
  return value;
}

std::expected<MouseButtonId, std::string> parseButton(std::string_view command, std::string_view token) {
  const std::string name = lower(token);
  if (name == "left") {
    return MouseButtonId::Left;
  }
  if (name == "right") {
    return MouseButtonId::Right;
  }
  if (name == "middle") {
    return MouseButtonId::Middle;
  }
  return std::unexpected(std::format("{}: unknown button '{}', expected left, right or middle", command, token));
}

std::expected<PointPx, std::string> parsePoint(std::string_view command, std::string_view x, std::string_view y) {
  const auto px = parseInteger(command, x, -kMaxCoordinate, kMaxCoordinate);
  if (!px) {
    return std::unexpected(px.error());
  }
  const auto py = parseInteger(command, y, -kMaxCoordinate, kMaxCoordinate);
  if (!py) {
    return std::unexpected(py.error());
  }
  return PointPx{static_cast<std::int32_t>(*px), static_cast<std::int32_t>(*py)};
}

using Words = std::vector<std::string_view>;
using Parsed = std::expected<Command, std::string>;

Parsed noArguments(std::string_view name, const Words& args, Command command) {
  if (!args.empty()) {
    return std::unexpected(std::format("{}: takes no arguments, got {}", name, plural(args.size())));
  }
  return command;
}

Parsed parseMove(const Words& args) {
  if (args.size() != 2) {
    return std::unexpected(std::format("move: expected X Y, got {}", plural(args.size())));
  }
  const auto at = parsePoint("move", args[0], args[1]);
  if (!at) {
    return std::unexpected(at.error());
  }
  return command::Move{*at};
}

Parsed parseButtonChange(std::string_view name, const Words& args, bool down) {
  MouseButtonId button = MouseButtonId::Left;
  if (args.size() > 1) {
    return std::unexpected(std::format("{}: expected [left|right|middle], got {}", name, plural(args.size())));
  }
  if (args.size() == 1) {
    const auto parsed = parseButton(name, args[0]);
    if (!parsed) {
      return std::unexpected(parsed.error());
    }
    button = *parsed;
  }
  if (down) {
    return command::Down{button};
  }
  return command::Up{button};
}

Parsed parseClick(const Words& args) {
  if (args.size() < 2 || args.size() > 3) {
    return std::unexpected(std::format("click: expected X Y [left|right|middle], got {}", plural(args.size())));
  }
  const auto at = parsePoint("click", args[0], args[1]);
  if (!at) {
    return std::unexpected(at.error());
  }
  MouseButtonId button = MouseButtonId::Left;
  if (args.size() == 3) {
    const auto parsed = parseButton("click", args[2]);
    if (!parsed) {
      return std::unexpected(parsed.error());
    }
    button = *parsed;
  }
  return command::Click{*at, button};
}

Parsed parseDoubleClick(const Words& args) {
  if (args.size() != 2) {
    return std::unexpected(std::format("dblclick: expected X Y, got {}", plural(args.size())));
  }
  const auto at = parsePoint("dblclick", args[0], args[1]);
  if (!at) {
    return std::unexpected(at.error());
  }
  return command::DoubleClick{*at};
}

Parsed parseDrag(const Words& args) {
  if (args.size() != 4 && args.size() != 5) {
    return std::unexpected(std::format("drag: expected X1 Y1 X2 Y2 [steps], got {}", plural(args.size())));
  }
  const auto from = parsePoint("drag", args[0], args[1]);
  if (!from) {
    return std::unexpected(from.error());
  }
  const auto to = parsePoint("drag", args[2], args[3]);
  if (!to) {
    return std::unexpected(to.error());
  }
  command::Drag drag;
  drag.from = *from;
  drag.to = *to;
  if (args.size() == 5) {
    const auto steps = parseInteger("drag", args[4], 1, kMaxDragSteps, "steps");
    if (!steps) {
      return std::unexpected(steps.error());
    }
    drag.steps = static_cast<std::uint32_t>(*steps);
  }
  return drag;
}

Parsed parseWheel(const Words& args) {
  if (args.size() != 2) {
    return std::unexpected(std::format("wheel: expected DX DY (notches), got {}", plural(args.size())));
  }
  const auto dx = parseInteger("wheel", args[0], -kMaxWheelNotches, kMaxWheelNotches);
  if (!dx) {
    return std::unexpected(dx.error());
  }
  const auto dy = parseInteger("wheel", args[1], -kMaxWheelNotches, kMaxWheelNotches);
  if (!dy) {
    return std::unexpected(dy.error());
  }
  return command::Wheel{static_cast<std::int32_t>(*dx), static_cast<std::int32_t>(*dy)};
}

std::expected<KeyChord, std::string> parseChordArgument(std::string_view name, const Words& args) {
  if (args.size() != 1) {
    return std::unexpected(
      std::format("{}: expected one key such as F5 or ctrl+shift+K, got {}", name, plural(args.size())));
  }
  auto chord = parseKeyChord(args[0]);
  if (!chord) {
    return std::unexpected(std::format("{}: {}", name, chord.error()));
  }
  return *chord;
}

Parsed parseCount(std::string_view name, const Words& args, std::int64_t low, std::int64_t high, bool frames) {
  if (args.size() != 1) {
    return std::unexpected(std::format("{}: expected {}, got {}", name, frames ? "N (frames)" : "MS (milliseconds)",
                                       plural(args.size())));
  }
  const auto value = parseInteger(name, args[0], low, high);
  if (!value) {
    return std::unexpected(value.error());
  }
  if (frames) {
    return command::Frames{static_cast<std::uint32_t>(*value)};
  }
  return command::Wait{static_cast<std::uint32_t>(*value)};
}

bool endsWithPng(std::string_view path) {
  if (path.size() < 4) {
    return false;
  }
  return lower(path.substr(path.size() - 4)) == ".png";
}

Parsed parseScreenshot(std::string_view rest) {
  std::string_view path = trim(rest);
  if (path.empty()) {
    return std::unexpected(std::string("screenshot: expected a path ending in .png and an optional scale"));
  }
  std::uint32_t scale = 1;
  if (!endsWithPng(path)) {
    const std::size_t split = path.find_last_of(" \t");
    if (split != std::string_view::npos) {
      const std::string_view last = path.substr(split + 1);
      std::int64_t value = 0;
      const auto [ptr, ec] = std::from_chars(last.data(), last.data() + last.size(), value);
      if (ec == std::errc{} && ptr == last.data() + last.size()) {
        if (value != 1 && value != 2 && value != 4) {
          return std::unexpected(std::format("screenshot: the scale {} must be 1, 2 or 4", value));
        }
        scale = static_cast<std::uint32_t>(value);
        path = trim(path.substr(0, split));
      }
    }
  }
  if (!endsWithPng(path)) {
    return std::unexpected(std::format("screenshot: the path '{}' must end in .png", path));
  }
  return command::Screenshot{std::string(path), scale};
}

Parsed parseWaitUntil(const Words& args) {
  if (args.empty()) {
    return std::unexpected(std::string("wait-until: expected capture-done, window NAME or no-window NAME, then TIMEOUT_MS"));
  }
  const std::string conditionWord = lower(args[0]);
  WaitCondition condition;
  if (conditionWord == "capture-done") {
    condition.kind = WaitCondition::Kind::CaptureDone;
  } else if (conditionWord == "window") {
    condition.kind = WaitCondition::Kind::WindowOpen;
  } else if (conditionWord == "no-window") {
    condition.kind = WaitCondition::Kind::WindowClosed;
  } else {
    return std::unexpected(std::format("wait-until: unknown condition '{}', expected capture-done, window NAME or "
                                       "no-window NAME",
                                       args[0]));
  }
  const bool named = condition.kind != WaitCondition::Kind::CaptureDone;
  if (args.size() < 2) {
    return std::unexpected(std::format("wait-until {}: expected {}TIMEOUT_MS, got no timeout", conditionWord,
                                       named ? "NAME " : ""));
  }
  const auto timeout = parseInteger("wait-until", args.back(), 0, kMaxWaitMs, "timeout");
  if (!timeout) {
    return std::unexpected(timeout.error());
  }
  if (named) {
    if (args.size() < 3) {
      return std::unexpected(std::format("wait-until {}: expected NAME TIMEOUT_MS, got no name", conditionWord));
    }
    for (std::size_t i = 1; i + 1 < args.size(); ++i) {
      condition.name += (i == 1 ? "" : " ");
      condition.name += args[i];
    }
  } else if (args.size() != 2) {
    return std::unexpected(std::format("wait-until capture-done: expected TIMEOUT_MS only, got {}", plural(args.size() - 1)));
  }
  return command::WaitUntil{std::move(condition), static_cast<std::uint32_t>(*timeout)};
}

Parsed parseFrameTimes(const Words& args) {
  if (args.empty()) {
    return command::FrameTimesQuery{FrameSelection{FrameSelection::Kind::SinceMark, 0}};
  }
  const std::string kind = lower(args[0]);
  const Words rest(args.begin() + 1, args.end());
  if (kind == "since-mark" || kind == "all") {
    if (!rest.empty()) {
      return std::unexpected(std::format("frame-times {}: takes no arguments, got {}", kind, plural(rest.size())));
    }
    return command::FrameTimesQuery{
      FrameSelection{kind == "all" ? FrameSelection::Kind::All : FrameSelection::Kind::SinceMark, 0}};
  }
  if (kind == "last") {
    if (rest.size() != 1) {
      return std::unexpected(std::format("frame-times last: expected N (frames), got {}", plural(rest.size())));
    }
    const auto count =
      parseInteger("frame-times", rest[0], 1, static_cast<std::int64_t>(kFrameTimesCapacity));
    if (!count) {
      return std::unexpected(count.error());
    }
    return command::FrameTimesQuery{FrameSelection{FrameSelection::Kind::Last, static_cast<std::uint32_t>(*count)}};
  }
  return std::unexpected(std::format("frame-times: expected since-mark, last N or all, got '{}'", args[0]));
}

Parsed parsePresentMode(const Words& args) {
  if (args.size() != 1) {
    return std::unexpected(std::format("present-mode: expected normal or immediate, got {}", plural(args.size())));
  }
  const std::string mode = lower(args[0]);
  if (mode == "normal") {
    return command::PresentMode{PresentModeRequest::Normal};
  }
  if (mode == "immediate") {
    return command::PresentMode{PresentModeRequest::Immediate};
  }
  return std::unexpected(std::format("present-mode: expected normal or immediate, got '{}'", args[0]));
}

}

std::expected<Command, std::string> parseCommand(std::string_view line) {
  std::string_view body = line;
  while (!body.empty() && (body.back() == '\r' || body.back() == '\n')) {
    body.remove_suffix(1);
  }
  while (!body.empty() && isSpace(body.front())) {
    body.remove_prefix(1);
  }
  if (trim(body).empty()) {
    return std::unexpected(std::string("empty command"));
  }
  std::size_t wordEnd = 0;
  while (wordEnd < body.size() && !isSpace(body[wordEnd])) {
    ++wordEnd;
  }
  const std::string word = lower(body.substr(0, wordEnd));
  const std::string_view rest = wordEnd < body.size() ? body.substr(wordEnd + 1) : std::string_view{};

  if (word == "text") {
    if (rest.empty()) {
      return std::unexpected(std::string("text: expected the characters to type"));
    }
    if (rest.size() > kMaxTextBytes) {
      return std::unexpected(std::format("text: {} bytes is more than the {} allowed", rest.size(), kMaxTextBytes));
    }
    return command::Text{std::string(rest)};
  }
  if (word == "screenshot") {
    return parseScreenshot(rest);
  }
  if (word == "dialog-save" || word == "dialog-open") {
    const std::string_view path = trim(rest);
    if (path.empty()) {
      return std::unexpected(std::format("{}: expected the path to answer with", word));
    }
    if (word == "dialog-save") {
      return command::DialogSave{std::string(path)};
    }
    return command::DialogOpen{std::string(path)};
  }

  const Words args = splitWords(rest);
  if (word == "move") {
    return parseMove(args);
  }
  if (word == "down") {
    return parseButtonChange("down", args, true);
  }
  if (word == "up") {
    return parseButtonChange("up", args, false);
  }
  if (word == "click") {
    return parseClick(args);
  }
  if (word == "dblclick") {
    return parseDoubleClick(args);
  }
  if (word == "drag") {
    return parseDrag(args);
  }
  if (word == "wheel") {
    return parseWheel(args);
  }
  if (word == "key" || word == "keydown" || word == "keyup") {
    const auto chord = parseChordArgument(word, args);
    if (!chord) {
      return std::unexpected(chord.error());
    }
    if (word == "key") {
      return command::KeyPress{*chord};
    }
    if (word == "keydown") {
      return command::KeyDown{*chord};
    }
    return command::KeyUp{*chord};
  }
  if (word == "frames") {
    return parseCount("frames", args, 1, kMaxFrames, true);
  }
  if (word == "wait") {
    return parseCount("wait", args, 0, kMaxWaitMs, false);
  }
  if (word == "windows") {
    return noArguments("windows", args, command::Windows{});
  }
  if (word == "hover") {
    return noArguments("hover", args, command::Hover{});
  }
  if (word == "info") {
    return noArguments("info", args, command::Info{});
  }
  if (word == "quit") {
    return noArguments("quit", args, command::Quit{});
  }
  if (word == "close") {
    return noArguments("close", args, command::Close{});
  }
  if (word == "dialog-cancel") {
    return noArguments("dialog-cancel", args, command::DialogCancel{});
  }
  if (word == "capture") {
    return noArguments("capture", args, command::Capture{});
  }
  if (word == "wait-until") {
    return parseWaitUntil(args);
  }
  if (word == "frame-mark") {
    return noArguments("frame-mark", args, command::FrameMark{});
  }
  if (word == "frame-times") {
    return parseFrameTimes(args);
  }
  if (word == "present-mode") {
    return parsePresentMode(args);
  }
  return std::unexpected(std::format("unknown command '{}'", body.substr(0, wordEnd)));
}

std::string_view commandName(const Command& command) {
  struct Namer {
    std::string_view operator()(const command::Move&) const { return "move"; }
    std::string_view operator()(const command::Down&) const { return "down"; }
    std::string_view operator()(const command::Up&) const { return "up"; }
    std::string_view operator()(const command::Click&) const { return "click"; }
    std::string_view operator()(const command::DoubleClick&) const { return "dblclick"; }
    std::string_view operator()(const command::Drag&) const { return "drag"; }
    std::string_view operator()(const command::Wheel&) const { return "wheel"; }
    std::string_view operator()(const command::KeyPress&) const { return "key"; }
    std::string_view operator()(const command::KeyDown&) const { return "keydown"; }
    std::string_view operator()(const command::KeyUp&) const { return "keyup"; }
    std::string_view operator()(const command::Text&) const { return "text"; }
    std::string_view operator()(const command::Frames&) const { return "frames"; }
    std::string_view operator()(const command::Wait&) const { return "wait"; }
    std::string_view operator()(const command::Screenshot&) const { return "screenshot"; }
    std::string_view operator()(const command::Windows&) const { return "windows"; }
    std::string_view operator()(const command::Hover&) const { return "hover"; }
    std::string_view operator()(const command::Info&) const { return "info"; }
    std::string_view operator()(const command::Quit&) const { return "quit"; }
    std::string_view operator()(const command::Close&) const { return "close"; }
    std::string_view operator()(const command::DialogSave&) const { return "dialog-save"; }
    std::string_view operator()(const command::DialogOpen&) const { return "dialog-open"; }
    std::string_view operator()(const command::DialogCancel&) const { return "dialog-cancel"; }
    std::string_view operator()(const command::Capture&) const { return "capture"; }
    std::string_view operator()(const command::WaitUntil&) const { return "wait-until"; }
    std::string_view operator()(const command::FrameMark&) const { return "frame-mark"; }
    std::string_view operator()(const command::FrameTimesQuery&) const { return "frame-times"; }
    std::string_view operator()(const command::PresentMode&) const { return "present-mode"; }
  };
  return std::visit(Namer{}, command);
}

}
