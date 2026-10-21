/*
 * SPDX-License-Identifier: MIT
 */

#include "pipe_name.h"

#include <format>

namespace bitoscope::gui_test_input {

namespace {

constexpr std::size_t kMaxTokenLength = 64;

bool allowed(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
}

}

std::expected<std::string, std::string> pipeNameForToken(std::string_view token) {
  if (token.empty()) {
    return std::unexpected(std::string("the token is empty"));
  }
  if (token.size() > kMaxTokenLength) {
    return std::unexpected(std::format("the token is {} characters long, at most {} are allowed", token.size(),
                                       kMaxTokenLength));
  }
  for (const char c : token) {
    if (allowed(c)) {
      continue;
    }
    std::string shown;
    if (c == ' ') {
      shown = "a space";
    } else if (static_cast<unsigned char>(c) < 0x20 || static_cast<unsigned char>(c) >= 0x7F) {
      shown = "a control or non-ASCII byte";
    } else {
      shown = std::format("'{}'", c);
    }
    return std::unexpected(
      std::format("the token contains {}; only letters, digits, '_' and '-' are allowed", shown));
  }
  return std::format("bitoscope_test_{}", token);
}

}
