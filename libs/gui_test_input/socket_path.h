/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>

namespace bitoscope::gui_test_input {

inline constexpr std::size_t kMaxSocketPathBytes = 107;

struct SocketDirectory {
  std::string path;
  bool privateFallback = false;
  friend bool operator==(const SocketDirectory&, const SocketDirectory&) = default;
};

SocketDirectory socketDirectory(std::optional<std::string_view> xdgRuntimeDir, std::uint32_t uid);

std::expected<std::string, std::string> socketPathForToken(std::string_view token, std::string_view directory);

}
