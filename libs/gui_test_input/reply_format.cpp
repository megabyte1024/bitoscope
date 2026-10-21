/*
 * SPDX-License-Identifier: MIT
 */

#include "reply_format.h"

#include <array>
#include <format>

namespace bitoscope::gui_test_input {

namespace {

std::string jsonBool(bool value) { return value ? "true" : "false"; }

std::string jsonNumberOrNull(bool present, std::uint32_t value) {
  return present ? std::to_string(value) : std::string("null");
}

}

std::string jsonString(std::string_view text) {
  std::string out = "\"";
  for (const char c : text) {
    switch (c) {
    case '"': out += "\\\""; break;
    case '\\': out += "\\\\"; break;
    case '\n': out += "\\n"; break;
    case '\r': out += "\\r"; break;
    case '\t': out += "\\t"; break;
    default:
      if (static_cast<unsigned char>(c) < 0x20) {
        out += std::format("\\u{:04x}", static_cast<unsigned>(static_cast<unsigned char>(c)));
      } else {
        out += c;
      }
      break;
    }
  }
  out += '"';
  return out;
}

std::string okReply(std::string_view detail) {
  if (detail.empty()) {
    return "ok";
  }
  return std::format("ok {}", detail);
}

std::string errorReply(std::string_view reason) { return std::format("error {}", reason); }

std::string windowsReply(const std::vector<WindowInfo>& windows) {
  std::string out = "ok [";
  bool first = true;
  for (const WindowInfo& w : windows) {
    if (!first) {
      out += ',';
    }
    first = false;
    out += std::format(
      "{{\"name\":{},\"x\":{},\"y\":{},\"w\":{},\"h\":{},\"focused\":{},\"hovered\":{},\"appearing\":{},"
      "\"child\":{},\"popup\":{},\"tooltip\":{}}}",
      jsonString(w.name), w.rect.x, w.rect.y, w.rect.w, w.rect.h, jsonBool(w.focused), jsonBool(w.hovered),
      jsonBool(w.appearing), jsonBool(w.child), jsonBool(w.popup), jsonBool(w.tooltip));
  }
  out += ']';
  return out;
}

std::string captureReply(const std::vector<SessionCapture>& captures) {
  std::string out = "ok [";
  bool first = true;
  for (const SessionCapture& c : captures) {
    if (!first) {
      out += ',';
    }
    first = false;
    out += std::format("{{\"session\":{},\"state\":{},\"stored\":{},\"requested\":{},\"reason\":{}}}",
                       jsonString(c.name), jsonString(c.state), c.stored, c.requested, jsonString(c.reason));
  }
  out += ']';
  return out;
}

std::string mouseCursorName(std::int32_t cursor) {
  // WHY: Order is Dear ImGui's ImGuiMouseCursor numbering, None (-1) first; a version bump may renumber it.
  static constexpr std::array<std::string_view, 12> kNames{
    "none", "arrow", "text-input", "resize-all", "resize-ns", "resize-ew", "resize-nesw", "resize-nwse",
    "hand", "wait", "progress", "not-allowed"};
  if (cursor >= -1 && cursor + 1 < static_cast<std::int32_t>(kNames.size())) {
    return std::string(kNames[static_cast<std::size_t>(cursor + 1)]);
  }
  return std::format("unknown:{}", cursor);
}

std::string hoverReply(const HoverInfo& hover) {
  const std::string mouse =
    hover.mouseValid ? std::format("{{\"x\":{},\"y\":{}}}", hover.mouse.x, hover.mouse.y) : std::string("null");
  const std::string window = hover.windowName.empty() ? std::string("null") : jsonString(hover.windowName);
  return std::format(
    "ok {{\"mouse\":{},\"window\":{},\"itemId\":{},\"active\":{},\"wantCaptureMouse\":{},"
    "\"wantCaptureKeyboard\":{},\"wantTextInput\":{}{}}}",
    mouse, window, jsonNumberOrNull(hover.hasItem, hover.itemId), jsonNumberOrNull(hover.activeId != 0, hover.activeId),
    jsonBool(hover.wantCaptureMouse), jsonBool(hover.wantCaptureKeyboard), jsonBool(hover.wantTextInput),
    hover.mouseCursor ? std::format(",\"cursor\":{}", jsonString(mouseCursorName(*hover.mouseCursor))) : std::string());
}

std::string infoReply(const InfoReply& info) {
  return std::format("ok {{\"width\":{},\"height\":{},\"scaleX\":{},\"scaleY\":{},\"frame\":{}}}", info.width,
                     info.height, info.scaleX, info.scaleY, info.frame);
}

std::string screenshotReply(std::string_view path, std::int32_t width, std::int32_t height) {
  return std::format("ok {} {}x{}", path, width, height);
}

std::string_view presentModeRequestName(PresentModeRequest request) {
  return request == PresentModeRequest::Normal ? "normal" : "immediate";
}

std::string frameMarkReply(std::uint64_t firstFrame) { return std::format("ok {{\"firstFrame\":{}}}", firstFrame); }

std::string frameTimesReply(const FrameTimesSummary& summary, std::string_view presentMode) {
  const auto us = [](std::uint64_t ns) { return (ns + 500) / 1000; };
  const auto stats = [&us](const DurationStats& d) {
    return std::format("{{\"min\":{},\"median\":{},\"p95\":{},\"p99\":{},\"max\":{}}}", us(d.minNs),
                       us(d.medianNs), us(d.p95Ns), us(d.p99Ns), us(d.maxNs));
  };
  std::string out = std::format(
    "ok {{\"records\":{},\"counted\":{},\"excluded\":{},\"open\":{},\"truncated\":{},\"lost\":{},"
    "\"firstFrame\":{},\"lastFrame\":{},\"breaks\":{},\"firstBreak\":",
    summary.records, summary.counted, summary.excluded, summary.open, jsonBool(summary.truncated), summary.lost,
    summary.firstFrame, summary.lastFrame, summary.breaks);
  if (summary.firstBreak) {
    out += std::format("{{\"kind\":\"{}\",\"frame\":{}}}",
                       summary.firstBreak->kind == FrameBreak::Kind::Missing ? "missing" : "duplicated",
                       summary.firstBreak->frame);
  } else {
    out += "null";
  }
  out += std::format(",\"presentMode\":{},\"spanUs\":{},\"periodUs\":{},\"workUs\":{},\"phasesUs\":{{",
                     jsonString(presentMode), us(summary.spanNs), stats(summary.period), stats(summary.work));
  for (std::size_t phase = 0; phase < kFramePhaseCount; ++phase) {
    out += std::format("{}\"{}\":{}", phase == 0 ? "" : ",", framePhaseName(static_cast<FramePhase>(phase)),
                       stats(summary.phases[phase]));
  }
  out += "},\"drawPartsUs\":{";
  for (std::size_t part = 0; part < kDrawPartCount; ++part) {
    out += std::format("\"{}\":{},", gui_frame_probe::drawPartName(static_cast<DrawPart>(part)),
                       stats(summary.drawParts[part]));
  }
  out += std::format("\"other\":{}}},\"longest\":[", stats(summary.drawOther));
  for (std::size_t i = 0; i < summary.longest.size(); ++i) {
    const LongFrame& f = summary.longest[i];
    out += std::format("{}{{\"frame\":{},\"atUs\":{},\"periodUs\":{},\"otherUs\":{},\"phasesUs\":{{",
                       i == 0 ? "" : ",", f.frame, us(f.atNs), us(f.periodNs), us(f.otherNs));
    for (std::size_t phase = 0; phase < kFramePhaseCount; ++phase) {
      out += std::format("{}\"{}\":{}", phase == 0 ? "" : ",", framePhaseName(static_cast<FramePhase>(phase)),
                         us(f.phaseNs[phase]));
    }
    out += "},\"drawPartsUs\":{";
    for (std::size_t part = 0; part < kDrawPartCount; ++part) {
      out += std::format("\"{}\":{},", gui_frame_probe::drawPartName(static_cast<DrawPart>(part)),
                         us(f.drawPartNs[part]));
    }
    out += std::format("\"other\":{}}}}}", us(f.drawOtherNs));
  }
  out += "],\"captures\":[";
  for (std::size_t i = 0; i < summary.marks.size(); ++i) {
    const CaptureMark& m = summary.marks[i];
    out += std::format("{}{{\"frame\":{},\"atUs\":{},\"session\":{},\"state\":{}}}", i == 0 ? "" : ",", m.frame,
                       us(m.atNs), jsonString(m.session), jsonString(m.state));
  }
  out += "]}";
  return out;
}

std::string presentModeReply(PresentModeRequest asked, std::string_view mode) {
  return std::format("ok {{\"asked\":{},\"mode\":{}}}", jsonString(presentModeRequestName(asked)), jsonString(mode));
}

}
