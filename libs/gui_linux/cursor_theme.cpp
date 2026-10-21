/*
 * SPDX-License-Identifier: MIT
 */

#include "cursor_theme.h"

#include <charconv>
#include <limits>

namespace bitoscope::gui_linux {

namespace {

bool isSpace(char c) {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

std::string_view trim(std::string_view text) {
  while (!text.empty() && isSpace(text.front())) {
    text.remove_prefix(1);
  }
  while (!text.empty() && isSpace(text.back())) {
    text.remove_suffix(1);
  }
  return text;
}

std::optional<std::string> kcminputrcMouseKey(std::string_view kcminputrc, std::string_view keyName) {
  bool inMouse = false;
  std::optional<std::string> found;
  while (!kcminputrc.empty()) {
    const std::size_t newline = kcminputrc.find('\n');
    std::string_view line = trim(kcminputrc.substr(0, newline));
    kcminputrc.remove_prefix(newline == std::string_view::npos ? kcminputrc.size() : newline + 1);
    if (line.empty() || line.front() == '#') {
      continue;
    }
    if (line.front() == '[') {
      inMouse = line.starts_with("[Mouse]");
      continue;
    }
    const std::size_t equals = line.find('=');
    if (!inMouse || equals == std::string_view::npos) {
      continue;
    }
    std::string_view key = trim(line.substr(0, equals));
    if (const std::size_t bracket = key.find('['); bracket != std::string_view::npos) {
      if (key.substr(bracket).starts_with("[$")) {
        key = key.substr(0, bracket);
      } else {
        continue;
      }
    }
    if (key != keyName) {
      continue;
    }
    const std::string_view value = trim(line.substr(equals + 1));
    found = value.empty() ? std::nullopt : std::optional<std::string>(value);
  }
  return found;
}

bool hasListEntry(std::string_view list, std::string_view entry) {
  while (!list.empty()) {
    const std::size_t colon = list.find(':');
    const std::string_view item = list.substr(0, colon);
    if (item == entry) {
      return true;
    }
    if (colon == std::string_view::npos) {
      break;
    }
    list.remove_prefix(colon + 1);
  }
  return false;
}

constexpr int kDefaultCursorSize = 24;

DesktopCursor fromKcminputrc(std::optional<std::string_view> kcminputrc) {
  DesktopCursor out;
  if (kcminputrc) {
    if (std::optional<std::string> theme = kcminputrcCursorTheme(*kcminputrc)) {
      out.theme = std::move(theme);
      out.size = kcminputrcCursorSize(*kcminputrc).value_or(kDefaultCursorSize);
    }
  }
  return out;
}

DesktopCursor fromPortal(std::optional<std::string_view> portalTheme,
                        std::optional<std::uint32_t> portalSize) {
  DesktopCursor out;
  if (portalTheme && !portalTheme->empty()) {
    out.theme = std::string(*portalTheme);
    const bool usable = portalSize && *portalSize > 0 &&
                        *portalSize <= static_cast<std::uint32_t>(std::numeric_limits<int>::max());
    out.size = usable ? static_cast<int>(*portalSize) : kDefaultCursorSize;
  }
  return out;
}

}

bool isKdeDesktop(std::optional<std::string_view> xdgCurrentDesktop) {
  return xdgCurrentDesktop.has_value() && hasListEntry(*xdgCurrentDesktop, "KDE");
}

std::optional<std::string> kcminputrcCursorTheme(std::string_view kcminputrc) {
  return kcminputrcMouseKey(kcminputrc, "cursorTheme");
}

std::optional<int> kcminputrcCursorSize(std::string_view kcminputrc) {
  const std::optional<std::string> text = kcminputrcMouseKey(kcminputrc, "cursorSize");
  if (!text) {
    return std::nullopt;
  }
  int size = 0;
  const auto [ptr, ec] = std::from_chars(text->data(), text->data() + text->size(), size);
  if (ec != std::errc() || ptr != text->data() + text->size() || size <= 0) {
    return std::nullopt;
  }
  return size;
}

DesktopCursor desktopCursor(std::optional<std::string_view> xdgCurrentDesktop,
                           std::optional<std::string_view> kcminputrc,
                           std::optional<std::string_view> portalTheme,
                           std::optional<std::uint32_t> portalSize) {
  const bool kde = isKdeDesktop(xdgCurrentDesktop);
  // WHY: The running desktop's source is tried first: a machine can hold both desktops' settings and they can disagree.
  if (kde) {
    if (DesktopCursor result = fromKcminputrc(kcminputrc); result.theme) {
      return result;
    }
    return fromPortal(portalTheme, portalSize);
  }
  if (DesktopCursor result = fromPortal(portalTheme, portalSize); result.theme) {
    return result;
  }
  return fromKcminputrc(kcminputrc);
}

}
