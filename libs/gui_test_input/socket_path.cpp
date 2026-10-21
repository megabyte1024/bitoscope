/*
 * SPDX-License-Identifier: MIT
 */

#include "socket_path.h"

#include <format>

#include "pipe_name.h"

namespace bitoscope::gui_test_input {

SocketDirectory socketDirectory(std::optional<std::string_view> xdgRuntimeDir, std::uint32_t uid) {
  if (xdgRuntimeDir && !xdgRuntimeDir->empty() && xdgRuntimeDir->front() == '/') {
    std::string_view path = *xdgRuntimeDir;
    while (path.size() > 1 && path.back() == '/') {
      path.remove_suffix(1);
    }
    return SocketDirectory{std::string(path), false};
  }
  return SocketDirectory{std::format("/tmp/bitoscope-{}", uid), true};
}

std::expected<std::string, std::string> socketPathForToken(std::string_view token, std::string_view directory) {
  const auto name = pipeNameForToken(token);
  if (!name) {
    return std::unexpected(name.error());
  }
  std::string path(directory);
  if (path.empty() || path.back() != '/') {
    path.push_back('/');
  }
  path += *name;
  path += ".sock";
  if (path.size() > kMaxSocketPathBytes) {
    return std::unexpected(std::format(
      "the socket path {} is {} bytes long, a Unix socket path holds at most {}; use a shorter token or a "
      "shorter XDG_RUNTIME_DIR",
      path, path.size(), kMaxSocketPathBytes));
  }
  return path;
}

}
