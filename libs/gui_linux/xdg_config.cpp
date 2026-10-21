/*
 * SPDX-License-Identifier: MIT
 */

#include "xdg_config.h"

namespace bitoscope::gui_linux {

namespace {

bool isAbsolute(std::optional<std::string_view> path) {
  return path && !path->empty() && path->front() == '/';
}

std::string join(std::string_view base, std::string_view name) {
  std::string out(base);
  if (out.back() != '/') {
    out += '/';
  }
  out += name;
  return out;
}

}

std::string configHome(std::optional<std::string_view> xdgConfigHome,
                       std::optional<std::string_view> home) {
  if (isAbsolute(xdgConfigHome)) {
    return std::string(*xdgConfigHome);
  }
  if (isAbsolute(home)) {
    return join(*home, ".config");
  }
  return {};
}

std::string configDirectory(std::optional<std::string_view> xdgConfigHome,
                            std::optional<std::string_view> home) {
  const std::string base = configHome(xdgConfigHome, home);
  return base.empty() ? base : join(base, "bitoscope");
}

}
