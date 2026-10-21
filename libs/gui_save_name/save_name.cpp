/*
 * SPDX-License-Identifier: MIT
 */

#include "save_name.h"

#include <cstddef>

namespace bitoscope::gui_save_name {

namespace {

constexpr std::string_view kSessionExtension = ".h5";

bool isAsciiLetter(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

bool isAsciiDigit(char c) {
  return c >= '0' && c <= '9';
}

}

bool hasFileExtension(std::string_view fileName) {
  const std::size_t dot = fileName.rfind('.');
  if (dot == std::string_view::npos || dot == 0 || dot + 1 == fileName.size()) {
    return false;
  }
  bool letter = false;
  for (const char c : fileName.substr(dot + 1)) {
    if (isAsciiLetter(c)) {
      letter = true;
    } else if (!isAsciiDigit(c)) {
      return false;
    }
  }
  return letter;
}

std::string withSessionExtension(std::string_view path) {
  const std::size_t separator = path.find_last_of("/\\");
  const std::string_view name = separator == std::string_view::npos ? path : path.substr(separator + 1);
  std::string out(path);
  if (name.empty() || hasFileExtension(name)) {
    return out;
  }
  out += name.ends_with('.') ? kSessionExtension.substr(1) : kSessionExtension;
  return out;
}

}
