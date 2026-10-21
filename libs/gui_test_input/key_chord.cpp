/*
 * SPDX-License-Identifier: MIT
 */

#include "key_chord.h"

#include <array>
#include <cstddef>
#include <format>
#include <utility>
#include <vector>

namespace bitoscope::gui_test_input {

namespace {

std::string lower(std::string_view text) {
  std::string out(text);
  for (char& c : out) {
    if (c >= 'A' && c <= 'Z') {
      c = static_cast<char>(c - 'A' + 'a');
    }
  }
  return out;
}

struct NamedKey {
  std::string_view name;
  KeyId key;
};

constexpr std::array<NamedKey, 26> kNamed{{
  {"enter", KeyId::Enter},       {"return", KeyId::Enter},   {"escape", KeyId::Escape},
  {"esc", KeyId::Escape},        {"delete", KeyId::Delete},  {"del", KeyId::Delete},
  {"backspace", KeyId::Backspace}, {"tab", KeyId::Tab},      {"space", KeyId::Space},
  {"left", KeyId::Left},         {"right", KeyId::Right},    {"up", KeyId::Up},
  {"down", KeyId::Down},         {"home", KeyId::Home},      {"end", KeyId::End},
  {"pageup", KeyId::PageUp},     {"pagedown", KeyId::PageDown}, {"insert", KeyId::Insert},
  {"equal", KeyId::Equal},       {"=", KeyId::Equal},        {"minus", KeyId::Minus},
  {"-", KeyId::Minus},           {"ins", KeyId::Insert},     {"pgup", KeyId::PageUp},
  {"pgdn", KeyId::PageDown},     {"spacebar", KeyId::Space},
}};

KeyId offsetKey(KeyId first, int offset) {
  return static_cast<KeyId>(static_cast<int>(first) + offset);
}

KeyId findKey(std::string_view text) {
  const std::string name = lower(text);
  if (name.size() == 1) {
    const char c = name[0];
    if (c >= 'a' && c <= 'z') {
      return offsetKey(KeyId::A, c - 'a');
    }
    if (c >= '0' && c <= '9') {
      return offsetKey(KeyId::Digit0, c - '0');
    }
  }
  if (name.size() >= 2 && name[0] == 'f') {
    int n = 0;
    bool digits = true;
    for (std::size_t i = 1; i < name.size(); ++i) {
      if (name[i] < '0' || name[i] > '9' || i > 2) {
        digits = false;
        break;
      }
      n = n * 10 + (name[i] - '0');
    }
    if (digits && n >= 1 && n <= 12) {
      return offsetKey(KeyId::F1, n - 1);
    }
  }
  if (name.size() == 7 && name.compare(0, 6, "numpad") == 0 && name[6] >= '0' && name[6] <= '9') {
    return offsetKey(KeyId::NumPad0, name[6] - '0');
  }
  for (const NamedKey& named : kNamed) {
    if (named.name == name) {
      return named.key;
    }
  }
  return KeyId::None;
}

int modifierIndex(std::string_view text) {
  const std::string name = lower(text);
  if (name == "ctrl" || name == "control") {
    return 0;
  }
  if (name == "shift") {
    return 1;
  }
  if (name == "alt") {
    return 2;
  }
  if (name == "super" || name == "win" || name == "cmd" || name == "meta") {
    return 3;
  }
  return -1;
}

}

std::expected<KeyChord, std::string> parseKeyChord(std::string_view text) {
  if (text.empty()) {
    return std::unexpected(std::string("empty key chord"));
  }
  std::vector<std::string_view> parts;
  std::size_t start = 0;
  for (;;) {
    const std::size_t plus = text.find('+', start);
    if (plus == std::string_view::npos) {
      parts.push_back(text.substr(start));
      break;
    }
    parts.push_back(text.substr(start, plus - start));
    start = plus + 1;
  }
  for (const std::string_view part : parts) {
    if (part.empty()) {
      return std::unexpected(std::format("empty key name in '{}'", text));
    }
  }
  KeyChord chord;
  std::array<bool, 4> seen{};
  for (std::size_t i = 0; i + 1 < parts.size(); ++i) {
    const int index = modifierIndex(parts[i]);
    if (index < 0) {
      if (findKey(parts[i]) != KeyId::None) {
        return std::unexpected(
          std::format("'{}' is a key, not a modifier: the key must be last in '{}'", parts[i], text));
      }
      return std::unexpected(
        std::format("unknown modifier '{}' (ctrl, shift, alt or super)", parts[i]));
    }
    if (seen[static_cast<std::size_t>(index)]) {
      return std::unexpected(std::format("modifier '{}' given twice in '{}'", parts[i], text));
    }
    seen[static_cast<std::size_t>(index)] = true;
  }
  chord.ctrl = seen[0];
  chord.shift = seen[1];
  chord.alt = seen[2];
  chord.super = seen[3];
  const std::string_view last = parts.back();
  if (modifierIndex(last) >= 0) {
    return std::unexpected(std::format("'{}' is a modifier, not a key: name the key after it", last));
  }
  chord.key = findKey(last);
  if (chord.key == KeyId::None) {
    return std::unexpected(std::format("unknown key '{}'", last));
  }
  return chord;
}

std::string_view keyName(KeyId key) {
  static constexpr std::array<std::string_view, 26> kLetters{
    "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M",
    "N", "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z"};
  static constexpr std::array<std::string_view, 10> kDigits{"0", "1", "2", "3", "4", "5", "6", "7", "8", "9"};
  static constexpr std::array<std::string_view, 12> kFunction{"F1", "F2", "F3", "F4",  "F5",  "F6",
                                                              "F7", "F8", "F9", "F10", "F11", "F12"};
  static constexpr std::array<std::string_view, 10> kPad{"NumPad0", "NumPad1", "NumPad2", "NumPad3", "NumPad4",
                                                         "NumPad5", "NumPad6", "NumPad7", "NumPad8", "NumPad9"};
  const int k = static_cast<int>(key);
  const auto within = [&](KeyId first, int count) {
    const int f = static_cast<int>(first);
    return k >= f && k < f + count;
  };
  if (within(KeyId::A, 26)) {
    return kLetters[static_cast<std::size_t>(k - static_cast<int>(KeyId::A))];
  }
  if (within(KeyId::Digit0, 10)) {
    return kDigits[static_cast<std::size_t>(k - static_cast<int>(KeyId::Digit0))];
  }
  if (within(KeyId::F1, 12)) {
    return kFunction[static_cast<std::size_t>(k - static_cast<int>(KeyId::F1))];
  }
  if (within(KeyId::NumPad0, 10)) {
    return kPad[static_cast<std::size_t>(k - static_cast<int>(KeyId::NumPad0))];
  }
  switch (key) {
  case KeyId::Enter: return "Enter";
  case KeyId::Escape: return "Escape";
  case KeyId::Delete: return "Delete";
  case KeyId::Backspace: return "Backspace";
  case KeyId::Tab: return "Tab";
  case KeyId::Space: return "Space";
  case KeyId::Left: return "Left";
  case KeyId::Right: return "Right";
  case KeyId::Up: return "Up";
  case KeyId::Down: return "Down";
  case KeyId::Home: return "Home";
  case KeyId::End: return "End";
  case KeyId::PageUp: return "PageUp";
  case KeyId::PageDown: return "PageDown";
  case KeyId::Insert: return "Insert";
  case KeyId::Equal: return "=";
  case KeyId::Minus: return "-";
  case KeyId::ModCtrl: return "Ctrl";
  case KeyId::ModShift: return "Shift";
  case KeyId::ModAlt: return "Alt";
  case KeyId::ModSuper: return "Super";
  default: return "None";
  }
}

}
