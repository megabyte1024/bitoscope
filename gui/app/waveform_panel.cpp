/*
 * SPDX-License-Identifier: MIT
 */

#include <imgui.h>
#include <imgui_internal.h>

#include <annotation_format.h>
#include <annotation_groups.h>
#include <annotation_lane_index.h>
#include <annotation_lane_layout.h>
#include <annotation_row_summary.h>
#include <annotation_shape.h>
#include <bracket.h>
#include <channel_colors.h>
#include <channel_render.h>
#include <channel_selection.h>
#include <color_choice.h>
#include <decoder_chain_config.h>
#include <decoder_header.h>
#include <decoder_labels.h>
#include <decoder_lane_model.h>
#include <decoder_lane_picture.h>
#include <lane_picture_cache.h>
#include <lane_text_source.h>
#include <annotation_row_layout.h>
#include <decoding_prefix.h>
#include <derived_lane_rows.h>
#include <lane_derivation.h>
#include <strip_cut.h>
#include <decoder_settings.h>
#include <annotation_text.h>
#include <row_data_format.h>
#include <word_list_text.h>
#include <channel_names.h>
#include <default_channel_colors.h>
#include <dimension_layout.h>
#include <display_format.h>
#include <double_click_marker.h>
#include <fixed_time.h>
#include <flag.h>
#include <frame_render.h>
#include <header_fit.h>
#include <interaction.h>
#include <lane_display.h>
#include <lane_header.h>
#include <lane_name_fit.h>
#include <text_field.h>
#include <lane_layout.h>
#include <lane_order.h>
#include <lane_pin_side.h>
#include <lane_regions.h>
#include <lanes_on_screen.h>
#include <byte_values.h>
#include <find_lanes.h>
#include <live_view.h>
#include <popup.h>
#include <pulse_measurement.h>
#include <signal_style.h>
#include <time_ruler.h>
#include <trigger_selection.h>
#include <waveform_pixel.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <exception>
#include <format>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "color_popup.h"
#include "find_box.h"
#include "gui_app.h"
#include "platform_services.h"
#include "session_waveform.h"
#include "theme.h"
#include "tr.h"
#include "waveform_renderer.h"

namespace bitoscope::gui {

namespace {

namespace wv = waveform_view;
namespace gc = gui_capture;
namespace gd = gui_decode;
namespace gwf = gui_waveform_frame;

constexpr float kRulerEm = 1.9f;
constexpr float kLaneEm = 2.2f;
constexpr float kLanePadEm = 0.3f;
constexpr float kDecoderLaneEm = 1.8f;
constexpr std::size_t kTooltipWordListChars = 1024;

class LaneClip {
 public:
  LaneClip(ImVec2 min, ImVec2 max) { ImGui::PushClipRect(min, max, true); }
  ~LaneClip() { ImGui::PopClipRect(); }
  LaneClip(const LaneClip&) = delete;
  LaneClip& operator=(const LaneClip&) = delete;
};

constexpr std::array<wv::LanePlacement, 3> kPlacements{wv::LanePlacement::Top, wv::LanePlacement::Scroll,
                                                       wv::LanePlacement::Bottom};

// WHY: WaveformRenderer::draw() keeps ONE lane list per view and frame, so each region uploads rows under its own id.
// Session ids never use the top two bits: regionView() and ghostView() set them.
std::uint64_t regionView(std::uint64_t view, wv::LanePlacement placement) {
  switch (placement) {
  case wv::LanePlacement::Top:
    return view | (std::uint64_t{1} << 62);
  case wv::LanePlacement::Bottom:
    return view | (std::uint64_t{2} << 62);
  case wv::LanePlacement::Scroll:
    break;
  }
  return view;
}

std::uint64_t ghostView(std::uint64_t view) {
  return view | (std::uint64_t{3} << 62);
}

constexpr std::array<decode::DataFormat, 3> kDataFormats{decode::DataFormat::Hex, decode::DataFormat::Dec,
                                                         decode::DataFormat::Ascii};

const char* dataFormatLabel(decode::DataFormat format) {
  switch (format) {
  case decode::DataFormat::Dec:
    return tr("Dec");
  case decode::DataFormat::Ascii:
    return tr("ASCII");
  case decode::DataFormat::Hex:
    break;
  }
  return tr("Hex");
}

const char* byteValueLabel(gd::ByteValueKind kind) {
  switch (kind) {
  case gd::ByteValueKind::U16Le:
    return tr("16-bit LE");
  case gd::ByteValueKind::U16Be:
    return tr("16-bit BE");
  case gd::ByteValueKind::U32Le:
    return tr("32-bit LE");
  case gd::ByteValueKind::U32Be:
    return tr("32-bit BE");
  case gd::ByteValueKind::F32Le:
    return tr("Float LE");
  case gd::ByteValueKind::F32Be:
    break;
  }
  return tr("Float BE");
}

void drawByteValueLines(const std::vector<gd::ByteValueLine>& lines, float em) {
  const float gap = em;
  float labelWidth = 0.0f;
  float valueWidth = 0.0f;
  for (const gd::ByteValueLine& line : lines) {
    labelWidth = (std::max)(labelWidth, ImGui::CalcTextSize(byteValueLabel(line.kind)).x);
    valueWidth = (std::max)(valueWidth, ImGui::CalcTextSize(line.text.c_str()).x);
  }
  ImGui::Separator();
  for (const gd::ByteValueLine& line : lines) {
    ImGui::TextUnformatted(byteValueLabel(line.kind));
    ImGui::SameLine(labelWidth + gap + valueWidth - ImGui::CalcTextSize(line.text.c_str()).x);
    ImGui::TextUnformatted(line.text.c_str());
  }
}

using ShownLane = wv::PlacedLane;

std::string laneWidgetId(const char* what, wv::LaneKey key) {
  return std::format("##{}_{}{}", what, key.kind == wv::LaneKind::Decoder ? 'd' : 'c', key.index);
}

const char* pinTooltip(wv::LanePlacement placement, wv::PinSide remembered) {
  switch (placement) {
  case wv::LanePlacement::Top:
    return tr("Pinned to top: click to unpin, right-click for more");
  case wv::LanePlacement::Bottom:
    return tr("Pinned to bottom: click to unpin, right-click for more");
  case wv::LanePlacement::Scroll:
    break;
  }
  return remembered == wv::PinSide::Top ? tr("Pin lane to top (right-click for more)")
                                        : tr("Pin lane to bottom (right-click for more)");
}
// WHY: Wave height stays a fixed fraction of the old pitch: a lane grown for its trigger row must not grow it.
constexpr float kBandTop = 0.2f;
constexpr float kBandBottom = 0.75f;

constexpr float kTriggerIconEm = 0.78f;
constexpr float kTriggerFramePadXEm = 0.14f;
constexpr float kTriggerFramePadYEm = 0.10f;
constexpr float kTriggerGapEm = 0.14f;
constexpr float kHeaderRowGapEm = 0.2f;

struct TriggerButtonSpec {
  Icon icon;
  trigger::MatchType type;
};

constexpr std::array<TriggerButtonSpec, 5> kTriggerButtons{{
  {Icon::TriggerRising, trigger::MatchType::kRising},
  {Icon::TriggerHigh, trigger::MatchType::kOne},
  {Icon::TriggerFalling, trigger::MatchType::kFalling},
  {Icon::TriggerLow, trigger::MatchType::kZero},
  {Icon::TriggerEdge, trigger::MatchType::kEdge},
}};
constexpr float kRulerLabelSpacing = 90.0f;
constexpr float kCursorGrab = 5.0f;
constexpr float kLaneDividerWidth = 6.0f;
constexpr float kBracketSlotEm = 0.8f;
constexpr bool kIntersectWithCurrentClipRect = false;

static_assert(sizeof(gui_lane_header::NameEdit::text) == gc::kChannelNameMaxBytes + 1,
              "name field holds exactly longest kept channel name");

struct KeyBinding {
  ImGuiKey imgui;
  wv::ViewKey view;
};
constexpr std::array<KeyBinding, 10> kKeys{{
  {ImGuiKey_Equal, wv::ViewKey::Equal},
  {ImGuiKey_Minus, wv::ViewKey::Minus},
  {ImGuiKey_KeypadAdd, wv::ViewKey::KeypadAdd},
  {ImGuiKey_KeypadSubtract, wv::ViewKey::KeypadSubtract},
  {ImGuiKey_Keypad0, wv::ViewKey::Keypad0},
  {ImGuiKey_0, wv::ViewKey::Digit0},
  {ImGuiKey_F, wv::ViewKey::F},
  {ImGuiKey_C, wv::ViewKey::C},
  {ImGuiKey_LeftArrow, wv::ViewKey::LeftArrow},
  {ImGuiKey_RightArrow, wv::ViewKey::RightArrow},
}};

wv::KeyModifiers modifiers(const ImGuiIO& io) {
  return wv::KeyModifiers{.ctrl = io.KeyCtrl, .shift = io.KeyShift, .alt = io.KeyAlt, .super = io.KeySuper};
}

ImU32 color32(wv::Rgb8 c, float alpha = 1.0f) {
  return IM_COL32(c.r, c.g, c.b, static_cast<int>(std::lround(alpha * 255.0f)));
}

std::uint32_t packColor(wv::Rgb8 c) { return color32(c); }

std::array<float, 4> color4(wv::Rgb8 c, float alpha = 1.0f) {
  return {c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, alpha};
}

wv::SignalStyle toWaveformViewStyle(gui_prefs::SignalStyle style) {
  return style == gui_prefs::SignalStyle::ChannelColors ? wv::SignalStyle::ChannelColors
                                                        : wv::SignalStyle::LevelColors;
}

ImU32 withAlpha(ImU32 color, float alpha) {
  ImVec4 c = ImGui::ColorConvertU32ToFloat4(color);
  c.w = alpha;
  return ImGui::ColorConvertFloat4ToU32(c);
}

// WHY: Scrollbar input runs before the frame is built from the viewport and its paint after the view moves,
// so the thumb and the traces show one viewport.
ImS64 scrollbarInput(const ImRect& bar, ImGuiID id, const wv::ScrollRange& range) {
  ImGuiWindow* window = ImGui::GetCurrentWindow();
  ImDrawList scratch(ImGui::GetDrawListSharedData());
  scratch._ResetForNewFrame();
  ImDrawList* real = window->DrawList;
  window->DrawList = &scratch;
  ImS64 position = range.position;
  ImGui::ScrollbarEx(bar, id, ImGuiAxis_X, &position, range.visible, range.contents, ImDrawFlags_RoundAll);
  window->DrawList = real;
  return position;
}

void scrollbarPaint(const ImRect& bar, ImGuiID id, const wv::ScrollRange& range) {
  ImGuiWindow* window = ImGui::GetCurrentWindow();
  const ImGuiStyle& style = ImGui::GetStyle();
  if (bar.GetWidth() <= 0.0f || bar.GetHeight() <= 0.0f) return;
  ImRect inner = bar;
  inner.Expand(-std::trunc((std::min)(style.ScrollbarPadding, (std::min)(bar.GetWidth(), bar.GetHeight()) * 0.5f)));
  const float length = inner.GetWidth();
  if (length < 1.0f) return;
  // WHY: int64_t is long on LP64 but ImS64 is long long: std::max cannot deduce one type, so cast to ImS64.
  const ImS64 whole =
    (std::max)((std::max)(static_cast<ImS64>(range.contents), static_cast<ImS64>(range.visible)), ImS64{1});
  const float grabMin = (std::min)(inner.GetWidth(), style.GrabMinSize);
  const float grabPixels = static_cast<float>(static_cast<int>(
    (std::clamp)(length * (static_cast<float>(range.visible) / static_cast<float>(whole)), grabMin, length)));
  const ImS64 scrollMax =
    (std::max)(ImS64{1}, static_cast<ImS64>(range.contents - range.visible));
  const float ratio = ImSaturate(static_cast<float>(range.position) / static_cast<float>(scrollMax));
  const float grabNorm = ratio * (length - grabPixels) / length;
  const bool held = ImGui::GetActiveID() == id;
  const bool hovered = ImGui::GetHoveredID() == id;
  window->DrawList->AddRectFilled(bar.Min, bar.Max, ImGui::GetColorU32(ImGuiCol_ScrollbarBg), window->WindowRounding,
                                  ImDrawFlags_RoundAll);
  const float grabLeft = ImLerp(inner.Min.x, inner.Max.x, grabNorm);
  window->DrawList->AddRectFilled(
    ImVec2(grabLeft, inner.Min.y), ImVec2(grabLeft + grabPixels, inner.Max.y),
    ImGui::GetColorU32(held ? ImGuiCol_ScrollbarGrabActive : hovered ? ImGuiCol_ScrollbarGrabHovered
                                                                      : ImGuiCol_ScrollbarGrab),
    style.ScrollbarRounding);
}

// WHY: One tr() call per case, not a table: extract_pot.py extracts string literals only.
const char* triggerButtonTooltip(trigger::MatchType type) {
  switch (type) {
  case trigger::MatchType::kRising:
    return tr("Trigger on rising edge");
  case trigger::MatchType::kOne:
    return tr("Trigger while high");
  case trigger::MatchType::kFalling:
    return tr("Trigger on falling edge");
  case trigger::MatchType::kZero:
    return tr("Trigger while low");
  case trigger::MatchType::kEdge:
    return tr("Trigger on any edge");
  case trigger::MatchType::kNone:
    break;
  }
  return "";
}

constexpr float kDimensionLabelPadXEm = 0.3f;
constexpr float kDimensionLabelPadYEm = 0.15f;

constexpr float kDimensionRowGapEm = 0.3f;

// WHY: Line ends go on pixel centres: Dear ImGui centres a stroke on its end points.
ImVec2 pixelCentre(float x, float y) { return ImVec2(x + 0.5f, y + 0.5f); }

ImVec2 imVec(wv::LanePoint p) { return ImVec2(p.x, p.y); }

void emitLanePicture(ImDrawList* draw, const wv::LanePicture& picture) {
  for (const wv::LanePrimitive& p : picture.primitives) {
    switch (p.kind) {
    case wv::LanePrimitiveKind::Rect:
      draw->AddRectFilled(imVec(p.points[0]), imVec(p.points[1]), p.color);
      break;
    case wv::LanePrimitiveKind::Polygon: {
      std::array<ImVec2, 6> points;
      for (std::size_t i = 0; i < p.pointCount; ++i) points[i] = imVec(p.points[i]);
      draw->AddConvexPolyFilled(points.data(), p.pointCount, p.color);
      draw->AddPolyline(points.data(), p.pointCount, p.outline, picture.lineWidth, ImDrawFlags_Closed);
      break;
    }
    case wv::LanePrimitiveKind::Text: {
      const std::string_view text = picture.textOf(p);
      draw->AddText(imVec(p.points[0]), p.color, text.data(), text.data() + text.size());
      break;
    }
    case wv::LanePrimitiveKind::Line:
      draw->AddLine(imVec(p.points[0]), imVec(p.points[1]), p.color, picture.lineWidth);
      break;
    case wv::LanePrimitiveKind::PushClip:
      draw->PushClipRect(imVec(p.points[0]), imVec(p.points[1]), true);
      break;
    case wv::LanePrimitiveKind::PopClip:
      draw->PopClipRect();
      break;
    }
  }
}

float dimensionLabelFrameHeight(float em) {
  return ImGui::GetTextLineHeight() + kDimensionLabelPadYEm * em * 2.0f;
}

void dimensionLine(ImDrawList* draw, float x0, float x1, float rowY, ImU32 color, const std::string& label,
                   float thickness, float viewLeft, float viewRight) {
  const float y = static_cast<float>(wv::dimensionRowY(rowY));
  // WHY: The row's centre, where arrowTriangle() puts the arrows' apex too.
  const float lineY = y + 0.5f;
  const ImGuiStyle& style = ImGui::GetStyle();
  const float em = ImGui::GetFontSize();
  const float tick = em * 0.35f;
  const float arrowLength = em * 0.5f;
  const float arrowWidth = em * 0.3f;
  const float gap = em * 0.3f;

  const float labelPadX = kDimensionLabelPadXEm * em;
  const float labelPadY = kDimensionLabelPadYEm * em;
  const ImVec2 textSize = ImGui::CalcTextSize(label.c_str());
  const float labelWidth = textSize.x + labelPadX * 2.0f;
  const float labelHeight = textSize.y + labelPadY * 2.0f;

  const wv::DimensionLayout layout =
    wv::layoutDimension(x0, x1, labelWidth, arrowLength, gap, viewLeft, viewRight);

  const float lo = (std::min)(x0, x1);
  const float hi = (std::max)(x0, x1);

  draw->AddLine(ImVec2(lo, lineY), ImVec2(hi, lineY), color, thickness);
  draw->AddLine(ImVec2(x0, lineY - tick), ImVec2(x0, lineY + tick), color, thickness);
  draw->AddLine(ImVec2(x1, lineY - tick), ImVec2(x1, lineY + tick), color, thickness);

  const int lengthPx = (std::max)(1, static_cast<int>(std::lround(arrowLength)));
  const int widthPx = wv::oddPixelWidth(arrowWidth);
  const auto directionOf = [](double tipX, double tailX) { return tailX >= tipX ? 1 : -1; };
  const wv::ArrowTriangle leftArrow = wv::arrowTriangle(
    layout.leftArrowEnd, y, directionOf(layout.leftArrowEnd, layout.leftArrowStart), lengthPx, widthPx);
  const wv::ArrowTriangle rightArrow = wv::arrowTriangle(
    layout.rightArrowEnd, y, directionOf(layout.rightArrowEnd, layout.rightArrowStart), lengthPx, widthPx);

  if (!layout.arrowsOutward) {
    draw->AddLine(ImVec2(lo, lineY), ImVec2(static_cast<float>(leftArrow.baseX), lineY), color, thickness);
    draw->AddLine(ImVec2(hi, lineY), ImVec2(static_cast<float>(rightArrow.baseX), lineY), color, thickness);
  }

  // WHY: Arrow vertices must be clockwise: in apex-top-bottom order right-pointing arrows lose anti-aliasing.
  const auto drawArrow = [&](const wv::ArrowTriangle& tri) {
    const auto v = wv::arrowVertices(tri);
    draw->AddTriangleFilled(ImVec2(static_cast<float>(v[0].x), static_cast<float>(v[0].y)),
                            ImVec2(static_cast<float>(v[1].x), static_cast<float>(v[1].y)),
                            ImVec2(static_cast<float>(v[2].x), static_cast<float>(v[2].y)), color);
  };
  drawArrow(leftArrow);
  drawArrow(rightArrow);

  const ImVec2 frameMin(static_cast<float>(layout.labelX), y - labelHeight * 0.5f);
  const ImVec2 frameMax(frameMin.x + labelWidth, frameMin.y + labelHeight);
  const ImU32 popupBg = ImGui::GetColorU32(ImGuiCol_PopupBg);
  const ImU32 border = ImGui::GetColorU32(ImGuiCol_Border);
  draw->AddRectFilled(frameMin, frameMax, popupBg, style.PopupRounding);
  if (style.PopupBorderSize > 0.0f) {
    addEdgeCentredOutline(draw, frameMin, frameMax, border, style.PopupRounding, style.PopupBorderSize);
  }
  draw->AddText(ImVec2(frameMin.x + labelPadX, y - textSize.y * 0.5f), color, label.c_str());
}

ImRect toImRect(const wv::FlagRect& r) {
  return ImRect(ImVec2(static_cast<float>(r.minX), static_cast<float>(r.minY)),
               ImVec2(static_cast<float>(r.maxX), static_cast<float>(r.maxY)));
}

ImVec2 cursorFlagSize() { return ImGui::CalcTextSize("0"); }

ImRect markerFlagRect(float x, float top, const std::string& name, float em) {
  const ImVec2 size = ImGui::CalcTextSize(name.c_str());
  return toImRect(wv::flagRect(x, top, size.x, size.y, em));
}


ImRect cursorFlagRect(float x, float top, float em) {
  const ImVec2 one = cursorFlagSize();
  return toImRect(wv::flagRectFor(x, top, false, {}, {one.x, one.y}, em));
}

void drawFlag(ImDrawList* draw, float x, float top, ImU32 color, const std::string& name, float em) {
  const ImVec2 label = ImGui::CalcTextSize(name.c_str());
  const ImVec2 one = cursorFlagSize();
  const ImRect rect = toImRect(
    wv::flagRectFor(x, top, !name.empty(), {label.x, label.y}, {one.x, one.y}, em));
  draw->AddRectFilled(rect.Min, rect.Max, color, em * 0.15f);
  if (!name.empty()) {
    const wv::FlagTextPos pos = wv::flagTextPosition(
      wv::FlagRect{rect.Min.x, rect.Min.y, rect.Max.x, rect.Max.y}, label.x, label.y);
    draw->AddText(ImVec2(static_cast<float>(pos.x), static_cast<float>(pos.y)), IM_COL32(255, 255, 255, 255),
                 name.c_str());
  }
}

// WHY: Open popups through gui_popup, never ImGui::OpenPopup(): ImGui names a popup by the current window's ID stack,
// and this one is begun in another window.
void openMarkerRename(SessionWaveform& waveform, wv::MarkerId id, gui_popup::Owner popupOwner) {
  const wv::Marker* marker = waveform.markers.find(id);
  if (marker == nullptr) {
    return;
  }
  waveform.renamingMarker = id;
  const std::size_t n = (std::min)(marker->name.size(), waveform.renameBuffer.size() - 1);
  std::copy(marker->name.begin(), marker->name.begin() + static_cast<std::ptrdiff_t>(n), waveform.renameBuffer.begin());
  waveform.renameBuffer[n] = '\0';
  waveform.renameFocusPending = true;
  gui_popup::open("marker_rename", popupOwner);
}

}

void GuiApp::retireWaveform(SessionUi& ui) {
  if (!ui.waveform) {
    return;
  }
  if (WaveformRenderer* renderer = platform_.waveformRenderer()) {
    for (const wv::LanePlacement placement : kPlacements) {
      renderer->release(regionView(ui.waveform->view, placement));
    }
    renderer->release(ghostView(ui.waveform->view));
  }
  ui.waveform.reset();
}

void GuiApp::applyLaneAction(SessionUi& ui, const wv::LaneActionResult& result) {
  if (result.changed) {
    ui.capture.markViewEdited();
  }
  if (result.record) {
    const std::optional<wv::PinSide> tabBefore = ui.lanePinSide;
    const wv::PinSide guiBefore = lanePinSide_;
    gui_prefs::recordLanePinned(*result.record, ui.lanePinSide, lanePinSide_);
    if (ui.lanePinSide != tabBefore || lanePinSide_ != guiBefore) {
      savePreferencesNow();
    }
  }
}

bool GuiApp::hasLanes(const SessionUi& ui) {
  return ui.waveform != nullptr && ui.waveform->sources.source() != nullptr && ui.waveform->sources.source()->channelCount() > 0;
}

void GuiApp::resetLaneOrder(SessionUi& ui) {
  if (!hasLanes(ui)) {
    return;
  }
  std::vector<std::uint32_t> channels = ui.waveform->sources.laneDevice();
  for (const wv::LaneEntry& entry : ui.laneOrder.entries()) {
    if (entry.key.kind == wv::LaneKind::Channel) {
      channels.push_back(entry.key.index);
    }
  }
  const std::vector<std::size_t> decoderOrder = gd::laneOrder(ui.decoders);
  wv::LaneOrder reset = wv::defaultLaneOrder(channels, decoderOrder);
  if (reset != ui.laneOrder) {
    ui.laneOrder = std::move(reset);
    ui.capture.markViewEdited();
  }
}

void GuiApp::waveformCommand(SessionUi& ui, wv::ViewCommand command, double anchorPixelX) {
  SessionWaveform* waveformPtr = ui.waveform.get();
  if (waveformPtr == nullptr || !waveformPtr->sources.source() || !waveformPtr->viewport) {
    return;
  }
  SessionWaveform& waveform = *waveformPtr;
  const std::uint64_t total = waveform.sources.source()->sampleCount();
  if (command == wv::ViewCommand::ToggleCursors) {
    if (!waveform.showCursors && !wv::canPlaceMarkersAndCursors(total)) {
      return;
    }
    waveform.showCursors = !waveform.showCursors;
    if (waveform.showCursors && !waveform.cursors) {
      waveform.cursors = wv::defaultCursors(*waveform.viewport, total);
    }
    ui.capture.markViewEdited();
    return;
  }
  const std::uint64_t viewTotal = waveform.sources.shows() == gui_waveform_frame::WaveformSourceChain::Shows::Live
                                    ? wv::liveViewTotal(total, waveform.sources.requestedSamples())
                                  : waveform.sources.shows() == gui_waveform_frame::WaveformSourceChain::Shows::Nothing
                                    ? wv::idleViewTotal(waveform.sources.requestedSamples())
                                    : total;
  wv::applyCommand(*waveform.viewport, command, anchorPixelX, viewTotal);
}

void GuiApp::trimMenuItems(gui_session::SessionId id, SessionUi& ui) {
  SessionWaveform& waveform = *ui.waveform;
  waveform.trimHovered.reset();
  if (!waveform.trimPoint) {
    return;
  }
  const std::uint64_t at = *waveform.trimPoint;
  const auto& kept = ui.capture.kept();
  const auto deletes = [&](gc::TrimSide side) {
    return kept && !ui.capture.saving() && gc::extentAfterTrim(kept->extent, side, at).has_value();
  };
  const bool deletesBefore = deletes(gc::TrimSide::Before);
  if (ImGui::MenuItem(tr("Trim before"), nullptr, false, deletesBefore)) {
    trimCapture(id, ui, gc::TrimSide::Before, at);
    return;
  }
  if (deletesBefore && ImGui::IsItemHovered()) {
    waveform.trimHovered = gc::TrimSide::Before;
  }
  const bool deletesAfter = deletes(gc::TrimSide::After);
  if (ImGui::MenuItem(tr("Trim after"), nullptr, false, deletesAfter)) {
    trimCapture(id, ui, gc::TrimSide::After, at);
    return;
  }
  if (deletesAfter && ImGui::IsItemHovered()) {
    waveform.trimHovered = gc::TrimSide::After;
  }
}

void GuiApp::trimCapture(gui_session::SessionId id, SessionUi& ui, gc::TrimSide side, std::uint64_t at) {
  SessionWaveform& waveform = *ui.waveform;
  waveform.trimPoint.reset();
  waveform.trimHovered.reset();
  const gui_session::Session* session = sessions_.find(id);
  const std::string name = session != nullptr ? session->name : std::to_string(id);
  const std::uint64_t held = ui.capture.kept() ? ui.capture.kept()->extent.count : 0;
  if (const auto trimmed = ui.capture.trim(side, at); !trimmed) {
    logger_.logf(log::LogLevel::warning, "capture", "{}: no samples deleted: {}", name, trimmed.error());
    return;
  }
  const std::uint64_t remain = ui.capture.kept()->extent.count;
  const std::uint64_t first = side == gc::TrimSide::Before ? at : 0;
  if (waveform.markers.trim(first, remain)) {
    if (waveform.selectedMarker && waveform.markers.find(*waveform.selectedMarker) == nullptr) {
      waveform.selectedMarker.reset();
    }
    if (waveform.contextMarker && waveform.markers.find(*waveform.contextMarker) == nullptr) {
      waveform.contextMarker.reset();
    }
  }
  if (waveform.cursors) {
    waveform.cursors = wv::cursorsAfterTrim(*waveform.cursors, first, remain);
    if (!waveform.cursors) {
      waveform.showCursors = false;
    }
  }
  if (waveform.viewport) {
    waveform.viewport->keepAfterTrim(first, remain);
  }
  logger_.logf(log::LogLevel::info, "capture", "{}: {} samples {} sample {} deleted; {} remain", name,
               gui_format::withThousands(held - remain), side == gc::TrimSide::Before ? "before" : "from",
               gui_format::withThousands(at), gui_format::withThousands(remain));
  runDecodersForSession(id, ui);
}

void GuiApp::drawWaveformToolbar(SessionUi& ui) {
  SessionWaveform* waveform = ui.waveform.get();
  const bool ready = waveform != nullptr && waveform->sources.source() && waveform->viewport;
  const double centre = ready ? waveform->viewport->widthPixels() * 0.5 : 0.0;
  ImGui::SameLine(0, ImGui::GetStyle().ItemSpacing.x * 3);
  ImGui::BeginDisabled(!ready);
  if (iconButton("zoom_in", Icon::ZoomIn, tr("Zoom in (+)"))) {
    waveformCommand(ui, wv::ViewCommand::ZoomIn, centre);
  }
  ImGui::SameLine();
  if (iconButton("zoom_out", Icon::ZoomOut, tr("Zoom out (-)"))) {
    waveformCommand(ui, wv::ViewCommand::ZoomOut, centre);
  }
  ImGui::SameLine();
  if (iconButton("zoom_fit", Icon::ZoomFit, tr("Zoom to fit (F)"))) {
    waveformCommand(ui, wv::ViewCommand::ZoomToFit, centre);
  }
  ImGui::SameLine();
  if (iconButton("zoom_1to1", Icon::Zoom1to1, tr("Zoom 1:1, one sample per pixel (0)"))) {
    waveformCommand(ui, wv::ViewCommand::Zoom1to1, centre);
  }
  ImGui::SameLine();
  const bool shown = ready && waveform->showCursors;
  const bool cursorsDisabled = ready && !shown && !wv::canPlaceMarkersAndCursors(waveform->sources.source()->sampleCount());
  ImGui::BeginDisabled(cursorsDisabled);
  constexpr bool mirrored = false;
  if (iconButton("cursors", Icon::Cursors, shown ? tr("Hide cursors (C)") : tr("Show cursors (C)"),
                 mirrored, shown)) {
    waveformCommand(ui, wv::ViewCommand::ToggleCursors, centre);
  }
  ImGui::EndDisabled();
  ImGui::EndDisabled();
}

void GuiApp::drawWaveform(gui_session::SessionId sessionId, SessionUi& ui, float headerWidthWanted, float width,
                          float tracesMinimum) {
  BITOSCOPE_DRAW_PART(waveformPart, frameProbe_, Prologue);
  const gui_popup::Owner popupOwner{sessionId};
  const ImGuiWindowFlags frame = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
  if (!ImGui::BeginChild("waveform", ImVec2(width, 0), ImGuiChildFlags_None, frame)) {
    ImGui::EndChild();
    return;
  }
  SessionWaveform* waveform = ui.waveform.get();
  const ImGuiIO& io = ImGui::GetIO();
  const ImGuiStyle& style = ImGui::GetStyle();
  const float em = ImGui::GetFontSize();
  const float scale = uiScale();
  const bool dark = darkInUse();
  WaveformRenderer* renderer = platform_.waveformRenderer();

  const auto centred = [](const char* text, bool problem, bool dark) {
    const ImVec2 size = ImGui::CalcTextSize(text);
    ImGui::SetCursorPos(ImVec2((std::max)(0.0f, (ImGui::GetWindowWidth() - size.x) * 0.5f),
                               (ImGui::GetWindowHeight() - size.y) * 0.5f));
    if (problem) {
      ImGui::PushStyleColor(ImGuiCol_Text, errorTextColor(dark));
      ImGui::TextUnformatted(text);
      ImGui::PopStyleColor();
    } else {
      ImGui::TextDisabled("%s", text);
    }
  };
  if (waveform == nullptr || waveform->sources.source() == nullptr || waveform->sources.source()->channelCount() == 0) {
    const bool problem = waveform != nullptr && !waveform->note.empty();
    centred(problem ? waveform->note.c_str() : tr("No capture yet"), problem, dark);
    ImGui::EndChild();
    return;
  }

  const wv::WaveformSource& source = *waveform->sources.source();
  const std::uint32_t channels = source.channelCount();
  const std::uint64_t total = source.sampleCount();
  const bool live = waveform->sources.shows() == gui_waveform_frame::WaveformSourceChain::Shows::Live;
  const bool idle = waveform->sources.shows() == gui_waveform_frame::WaveformSourceChain::Shows::Nothing;
  const std::uint64_t viewTotal = live   ? wv::liveViewTotal(total, waveform->sources.requestedSamples())
                                  : idle ? wv::idleViewTotal(waveform->sources.requestedSamples())
                                         : total;
  const auto laneChoice = [&](std::uint32_t lane) -> const wv::ColorChoice& {
    static const wv::ColorChoice kFallback = wv::ColorChoice::palette(0);
    const std::uint32_t device = lane < waveform->sources.laneDevice().size() ? waveform->sources.laneDevice()[lane] : lane;
    if (device < ui.channelColors.size()) {
      return ui.channelColors[device];
    }
    return kFallback;
  };
  const auto laneTrigger = [&](std::uint32_t lane) -> trigger::MatchType* {
    const std::uint32_t device = lane < waveform->sources.laneDevice().size() ? waveform->sources.laneDevice()[lane] : lane;
    if (!ui.settings || device >= ui.settings->trigger.size()) {
      return nullptr;
    }
    return &ui.settings->trigger[device];
  };
  const std::vector<std::uint32_t> visible =
    ui.settings ? wv::visibleLanes(waveform->sources.laneDevice(), ui.settings->channels)
                : wv::shownLanes(waveform->sources.laneDevice().size(), ui.fileLanesShown);
  const float rulerHeight = std::round(em * kRulerEm);
  const float basePitch = std::round(em * kLaneEm);
  const float triggerIconSize = std::round(em * kTriggerIconEm);
  const std::uint32_t triggerIconPixels =
    static_cast<std::uint32_t>((std::max)(1.0f, triggerIconSize));
  const ImVec2 triggerFramePadding(std::round(em * kTriggerFramePadXEm),
                                   std::round(em * kTriggerFramePadYEm));
  const float triggerGap = std::round(em * kTriggerGapEm);
  const float triggerRowHeight = triggerIconSize + triggerFramePadding.y * 2.0f;
  const float headerRowGap = std::round(em * kHeaderRowGapEm);
  const float headerContentHeight = em + headerRowGap + triggerRowHeight;
  const float waveHeight = basePitch * (kBandBottom - kBandTop);
  const float labelFrameHeight = dimensionLabelFrameHeight(em);
  const float dimensionRowGap = std::round(em * kDimensionRowGapEm);
  const float dimensionsPitch =
    static_cast<float>(wv::dimensionRowsPitch(waveHeight, labelFrameHeight, dimensionRowGap));
  const float pitch = wv::lanePitch(wv::lanePitch(basePitch, headerContentHeight), dimensionsPitch);
  const wv::WaveBand waveOffsets = wv::waveBand(pitch, waveHeight);
  const float lanePad = std::round(em * kLanePadEm);
  const std::size_t decoderCount = ui.decoders.size();
  const float decoderPitch = std::round(em * kDecoderLaneEm);
  std::vector<gd::DecoderReads> decoderReads(decoderCount);
  std::vector<bool> hasBracket(decoderCount, false);
  for (std::size_t d = 0; d < decoderCount; ++d) {
    decoderReads[d] = gd::decoderReads(ui.decoders, d);
    const bool readsShownChannel = std::ranges::any_of(decoderReads[d].channels, [&](std::uint32_t device) {
      return std::ranges::any_of(visible, [&](std::uint32_t lane) {
        return lane < waveform->sources.laneDevice().size() && waveform->sources.laneDevice()[lane] == device;
      });
    });
    hasBracket[d] = readsShownChannel || !decoderReads[d].decoders.empty();
  }
  const std::vector<std::optional<std::size_t>> bracketSlotOf = wv::bracketSlots(hasBracket);
  const float bracketStep = std::round(em * kBracketSlotEm);
  const float gutterWidth = static_cast<float>(wv::bracketGutterWidth(
    static_cast<std::size_t>(std::ranges::count_if(bracketSlotOf, [](const auto& slot) { return slot.has_value(); })),
    bracketStep));
  wv::LaneNameFitInput nameFit;
  nameFit.available = ImGui::GetContentRegionAvail().x;
  nameFit.gutter = gutterWidth;
  nameFit.wanted = headerWidthWanted;
  nameFit.laneMinimum = gui_prefs::laneNameWidthPixels(gui_prefs::kLaneNameWidthMinCentiEm, em, 0.0f);
  nameFit.tracesMinimum = tracesMinimum;
  const wv::LaneNameFit nameLimit = wv::fitLaneNameWidth(nameFit);
  const float headerWidth = std::floor(static_cast<float>(nameLimit.laneNameWidth));
  const float headerWidest = static_cast<float>(nameLimit.widest);
  ui.laneLabels.refresh(ui.decoders, decoderRegistry());
  std::vector<gd::DecoderLaneModel> decoderLanes;
  decoderLanes.reserve(decoderCount);
  std::vector<double> decoderHeights(decoderCount, 0.0);
  for (std::size_t d = 0; d < decoderCount; ++d) {
    gd::DecoderLaneModelInput laneInput;
    laneInput.store = &ui.decoderAnnotations;
    laneInput.index = d;
    laneInput.labels = &ui.laneLabels.at(d);
    laneInput.capture = ui.laneView ? &*ui.laneView : nullptr;
    laneInput.showDerived = showDerivedRows_;
    laneInput.rowPitch = decoderPitch;
    decoderLanes.push_back(gd::decoderLaneModel(laneInput));
    decoderHeights[d] = decoderLanes[d].height;
  }
  const float framebufferScale =
    io.DisplayFramebufferScale.x > 0.0f ? io.DisplayFramebufferScale.x : 1.0f;
  const float lineWidth = (std::max)(1.0f, std::round(scale * framebufferScale)) / framebufferScale;
  // WHY: Dear ImGui anti-aliases a fractional fill edge: snap edges to the pixels they cover to stay crisp.
  const auto fillPixels = [framebufferScale](ImDrawList* list, ImVec2 min, ImVec2 max, ImU32 color) {
    const auto edge = [&](float v) { return static_cast<float>(wv::filledEdge(v, framebufferScale)); };
    list->AddRectFilled(ImVec2(edge(min.x), edge(min.y)), ImVec2(edge(max.x), edge(max.y)), color);
  };
  const ImVec2 cursorStart = ImGui::GetCursorScreenPos();
  const ImVec2 origin(cursorStart.x + gutterWidth, cursorStart.y);
  const float selectionFrameWidth = 2.0f * scale;
  const float flagTop = origin.y + selectionFrameWidth;
  const float fullWidth = ImGui::GetContentRegionAvail().x - gutterWidth;
  const float fullHeight = ImGui::GetContentRegionAvail().y;
  const float scrollbarHeight = style.ScrollbarSize;
  const float lanesHeight = (std::max)(pitch, fullHeight - rulerHeight - scrollbarHeight);
  ui.laneOrder.sync(waveform->sources.laneDevice(), decoderCount);
  const bool laneDragging = waveform->laneDrag.dragged.has_value();
  const wv::LaneDisplay allLanes =
    wv::laneDisplay(ui.laneOrder, waveform->sources.laneDevice(), visible, decoderHeights, pitch, lanePad);
  const wv::LaneDisplay laneDisplay =
    laneDragging ? wv::withoutLane(allLanes, *waveform->laneDrag.dragged) : allLanes;
  const wv::LaneRegions3 regions =
    wv::laneRegions3(lanesHeight, laneDisplay.scrollContentHeight, laneDisplay.topHeight, laneDisplay.bottomHeight,
                     pitch, laneDragging, decoderPitch);
  const float contentHeight = static_cast<float>(regions.contentHeight);
  const bool lanesScroll = regions.maxScroll > 0.0;
  const float traceLeft = origin.x + headerWidth;
  const float traceWidth =
    std::floor((std::max)(1.0f, fullWidth - headerWidth - (lanesScroll ? style.ScrollbarSize : 0.0f)));
  const std::uint32_t widthPixels = static_cast<std::uint32_t>(traceWidth);

  if (!waveform->viewport) {
    waveform->viewport.emplace(source.sampleRateMilliHz(), widthPixels);
  }
  wv::Viewport& viewport = *waveform->viewport;
  viewport.setWidthPixels(widthPixels);
  viewport.setTimeOriginSample(static_cast<std::int64_t>(waveform->sources.timeZero()));
  if (!waveform->fitted) {
    if (live) {
      wv::startLiveView(viewport, waveform->carriedZoom, viewTotal);
    } else if (idle) {
      wv::fitIdleView(viewport, waveform->sources.requestedSamples());
    } else {
      viewport.zoomToFit(viewTotal);
    }
    waveform->fitted = true;
  } else {
    if (live) {
      wv::followLiveEdge(viewport, waveform->lastTotal, total);
    }
    viewport.clampToLimits(viewTotal);
  }
  waveform->lastTotal = total;
  const std::uint64_t firstShownSample =
    static_cast<std::uint64_t>((std::max)(0.0, std::floor(viewport.firstVisibleSample())));
  const std::optional<gd::FindMatch> findReveal = updateFind(ui, allLanes, decoderLanes, firstShownSample);
  if (findReveal) {
    wv::panToShow(viewport, findReveal->begin, findReveal->end, viewTotal);
  }
  const ImRect scrollBar(ImVec2(traceLeft, origin.y + rulerHeight + lanesHeight),
                         ImVec2(traceLeft + traceWidth, origin.y + rulerHeight + lanesHeight + scrollbarHeight));
  const ImGuiID scrollBarId = ImGui::GetID("hscroll");
  {
    const wv::ScrollRange before = wv::scrollRangeFor(viewport, viewTotal);
    const ImS64 dragged = scrollbarInput(scrollBar, scrollBarId, before);
    if (dragged != before.position) {
      wv::scrollTo(viewport, dragged, viewTotal);
    }
  }
  const std::uint32_t columnCount =
    framebufferScale == 1.0f
      ? widthPixels
      : (std::max)(1u, static_cast<std::uint32_t>(std::lround(traceWidth * framebufferScale)));
  const auto columns = [&] { return viewport.resampled(columnCount); };
  const auto columnX = [&](std::uint64_t sample) {
    return static_cast<float>(wv::overlayColumnX(
      traceLeft, framebufferScale, columns().samplePixelX(static_cast<int64_t>(sample))));
  };

  const ImU32 textColor = ImGui::GetColorU32(ImGuiCol_Text);
  const ImU32 mutedColor = ImGui::GetColorU32(ImGuiCol_TextDisabled);
  const ImU32 accent = ImGui::GetColorU32(ImGuiCol_SliderGrab);
  // WHY: Selection frame uses ImGuiCol_Header, the toolbar's selected-button fill, not accent (owner decision).
  const ImU32 selectionColor = ImGui::GetColorU32(ImGuiCol_Header);
  const ImU32 background = color32(wv::traceBackground(dark));
  const ImU32 gridColor = withAlpha(mutedColor, dark ? 0.18f : 0.22f);
  std::vector<wv::RulerTick> ticks;

  ImGui::SetCursorScreenPos(ImVec2(traceLeft, origin.y));
  ImGui::InvisibleButton("ruler", ImVec2(traceWidth, rulerHeight));
  const bool rulerHovered = ImGui::IsItemHovered();
  const bool rulerActive = ImGui::IsItemActive();
  const double rulerMouseX = static_cast<double>(io.MousePos.x - traceLeft);
  const float markerGrab = kCursorGrab * scale;
  const auto widthLabelGeometry =
    [&](const wv::CursorPair& pair) -> std::optional<std::pair<std::string, wv::FlagRect>> {
    if (!waveform->showCursors) return std::nullopt;
    const std::string time = gui_format::formatTimePs(pair.deltaTimePicoseconds(source.sampleRateMilliHz()));
    const auto frequency = pair.deltaFrequencyMilliHz(source.sampleRateMilliHz());
    const std::string label =
      frequency ? std::format("{} | {}", time, gui_format::formatFrequencyRounded(*frequency, 2)) : time;
    const ImVec2 size = ImGui::CalcTextSize(label.c_str());
    const double midpointX = (columnX(pair.first()) + columnX(pair.second())) * 0.5;
    const wv::FlagRect rect =
      wv::widthLabelRect(midpointX, static_cast<double>(traceLeft), static_cast<double>(traceLeft + traceWidth),
                         static_cast<double>(origin.y), static_cast<double>(rulerHeight), size.x, size.y, em);
    return std::make_pair(label, rect);
  };
  const auto overWidthLabelAt = [&](const wv::CursorPair& pair) {
    const auto geometry = widthLabelGeometry(pair);
    return geometry && wv::flagRectContains(geometry->second, static_cast<double>(io.MousePos.x),
                                            static_cast<double>(io.MousePos.y));
  };
  const wv::CursorPair cursorsForGrab = waveform->cursors.value_or(wv::CursorPair(0, 0));
  const bool cursorsGrabbable = waveform->showCursors && waveform->cursors.has_value();
  if (ImGui::IsItemActivated() && ImGui::IsMousePosValid()) {
    const wv::GrabTarget lineFallback =
      wv::grabAtPixel(waveform->markers, cursorsForGrab, cursorsGrabbable, viewport, rulerMouseX, markerGrab);
    const std::vector<std::string> pressLabels =
      wv::markerLabels(waveform->markers, waveform->selectedMarker, std::nullopt, source.sampleRateMilliHz());
    const std::vector<wv::Marker>& markersForPress = waveform->markers.markers();
    std::vector<wv::RulerFlagCandidate> pressCandidates;
    pressCandidates.reserve(markersForPress.size() + 2);
    for (std::size_t i = 0; i < markersForPress.size(); ++i) {
      const float mx = std::round(columnX(markersForPress[i].sample));
      const ImRect r = markerFlagRect(mx, flagTop, pressLabels[i], em);
      pressCandidates.push_back(wv::RulerFlagCandidate{
        wv::GrabKind::Marker, markersForPress[i].id, mx, wv::FlagRect{r.Min.x, r.Min.y, r.Max.x, r.Max.y}});
    }
    if (cursorsGrabbable) {
      const float x0 = std::round(columnX(cursorsForGrab.first()));
      const float x1 = std::round(columnX(cursorsForGrab.second()));
      const ImRect r0 = cursorFlagRect(x0, flagTop, em);
      const ImRect r1 = cursorFlagRect(x1, flagTop, em);
      pressCandidates.push_back(wv::RulerFlagCandidate{
        wv::GrabKind::CursorFirst, 0, x0, wv::FlagRect{r0.Min.x, r0.Min.y, r0.Max.x, r0.Max.y}});
      pressCandidates.push_back(wv::RulerFlagCandidate{
        wv::GrabKind::CursorSecond, 0, x1, wv::FlagRect{r1.Min.x, r1.Min.y, r1.Max.x, r1.Max.y}});
    }
    const wv::GrabTarget grabbed = wv::rulerPressTarget(
      pressCandidates, static_cast<double>(io.MousePos.x), static_cast<double>(io.MousePos.y),
      static_cast<double>(origin.y), static_cast<double>(origin.y + rulerHeight), markerGrab, lineFallback);
    waveform->grab.begin(grabbed, rulerMouseX, cursorsForGrab);
    waveform->rulerPressDragged = false;
  }
  if (rulerActive && ImGui::IsMousePosValid()) {
    const bool rulerDragged = waveform->rulerPressDragged || ImGui::IsMouseDragging(ImGuiMouseButton_Left);
    if (waveform->grab.dragTo(waveform->markers, waveform->cursors, viewport, rulerMouseX, total, rulerDragged) ==
        wv::DragOutcome::Moved) {
      ui.capture.markViewEdited();
    }
  }
  if (rulerActive && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
    waveform->rulerPressDragged = true;
    if (waveform->grab.marker()) {
      waveform->selectedMarker = waveform->grab.marker();
    }
  }
  if (ImGui::IsItemDeactivated() && waveform->grab.selectsMarkerOnRelease(waveform->rulerPressDragged)) {
    waveform->selectedMarker = waveform->grab.marker();
  }
  if (rulerHovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
    if (const auto hit = wv::markerUnderPixel(waveform->markers, viewport, rulerMouseX, markerGrab)) {
      openMarkerRename(*waveform, *hit, popupOwner);
    } else if (const auto at = wv::markerForDoubleClick(
                 wv::DoubleClickPress{wv::PointerRegion::Ruler, rulerMouseX,
                                      ImGui::GetMouseClickedCount(ImGuiMouseButton_Left)},
                 viewport, total)) {
      waveform->markers.add(*at);
      ui.capture.markViewEdited();
    }
  }
  if (rulerHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
    if (const auto hit = wv::markerUnderPixel(waveform->markers, viewport, rulerMouseX, markerGrab)) {
      waveform->contextMarker = hit;
      gui_popup::open("marker_context", popupOwner);
    }
  }
  const std::vector<std::string> unhoveredLabels =
    wv::markerLabels(waveform->markers, waveform->selectedMarker, std::nullopt, source.sampleRateMilliHz());
  std::optional<wv::MarkerId> hoveredFlag;
  if (rulerHovered && ImGui::IsMousePosValid()) {
    const std::vector<wv::Marker>& markerList = waveform->markers.markers();
    for (std::size_t i = 0; i < markerList.size(); ++i) {
      const float mx = std::round(
        columnX(markerList[i].sample));
      if (markerFlagRect(mx, flagTop, unhoveredLabels[i], em).Contains(io.MousePos)) {
        hoveredFlag = markerList[i].id;
        break;
      }
    }
  }
  bool overCursorFlag = false;
  if (rulerHovered && ImGui::IsMousePosValid() && waveform->showCursors && waveform->cursors) {
    const float x0 = std::round(columnX(waveform->cursors->first()));
    const float x1 = std::round(columnX(waveform->cursors->second()));
    overCursorFlag = cursorFlagRect(x0, flagTop, em).Contains(io.MousePos) ||
                    cursorFlagRect(x1, flagTop, em).Contains(io.MousePos);
  }
  const bool lineHit =
    rulerHovered &&
    wv::grabAtPixel(waveform->markers, cursorsForGrab, cursorsGrabbable, viewport, rulerMouseX, markerGrab)
        .kind != wv::GrabKind::None;
  const wv::PointerShape rulerShape =
    wv::rulerPointerShape(waveform->grab, hoveredFlag.has_value(), overCursorFlag, lineHit);
  if (rulerShape == wv::PointerShape::ResizeEW) {
    ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
  } else if (rulerShape == wv::PointerShape::Arrow) {
    ImGui::SetMouseCursor(ImGuiMouseCursor_Arrow);
  }

  ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + rulerHeight));
  const ImGuiWindowFlags lanesFlags =
    ImGuiWindowFlags_NoScrollWithMouse | (lanesScroll ? ImGuiWindowFlags_AlwaysVerticalScrollbar
                                                      : ImGuiWindowFlags_NoScrollbar);
  bool tracesHovered = false;
  bool tracesActive = false;
  if (ImGui::BeginChild("lanes", ImVec2(fullWidth, lanesHeight), ImGuiChildFlags_None, lanesFlags)) {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 lanesPos = ImGui::GetWindowPos();
    const float visibleTop = lanesPos.y;
    const float visibleBottom = lanesPos.y + lanesHeight;
    // WHY: Marker and cursor lines are ONE AddLine() across ruler and lanes: two anti-aliased segments leave a seam row.
    // Drawn in the lanes child with an unclipped rect: a child's draw list is painted after the ruler's.
    const float flagBottomY = cursorFlagRect(0.0f, flagTop, em).Max.y;
    // WHY: Clamp scroll against THIS frame content: ImGui clamps against the last one, so hidden lanes land off view.
    float laneScroll = static_cast<float>(
      wv::laneScrollInRange(ImGui::GetScrollY(), contentHeight, lanesHeight));
    if (findReveal && findReveal->decoder < decoderCount) {
      const gd::DecoderLaneModel& revealLane = decoderLanes[findReveal->decoder];
      if (const std::optional<double> rowTop =
            gd::findRowScrollTop(laneDisplay, findReveal->decoder, gd::shownRowSlot(revealLane.rows, findReveal->row),
                                 decoderPitch)) {
        laneScroll = static_cast<float>(std::clamp(
          wv::scrollToShowRow(laneScroll, regions.scrollHeight, *rowTop, decoderPitch), 0.0, regions.maxScroll));
      }
    }
    if (laneScroll != ImGui::GetScrollY()) {
      ImGui::SetScrollY(laneScroll);
    }
    ImGui::SetCursorPos(ImVec2(0, 0));
    ImGui::Dummy(ImVec2(1.0f, contentHeight));

    const auto regionBand = [&](wv::LanePlacement placement) {
      const double top = placement == wv::LanePlacement::Top      ? regions.topTop
                         : placement == wv::LanePlacement::Bottom ? regions.bottomTop
                                                                  : regions.scrollTop;
      const double height = placement == wv::LanePlacement::Top      ? regions.topHeight
                            : placement == wv::LanePlacement::Bottom ? regions.bottomHeight
                                                                     : regions.scrollHeight;
      return std::pair<float, float>(visibleTop + static_cast<float>(top), visibleTop + static_cast<float>(top + height));
    };
    wv::PlacedLanes lanePlacement = wv::placeLanes(laneDisplay, regions, visibleTop, laneScroll);
    const std::vector<ShownLane> shownLanes = std::move(lanePlacement.onScreen);
    const std::vector<ShownLane> placedLanes = std::move(lanePlacement.placed);
    const auto [scrollAreaTop, scrollAreaBottom] = regionBand(wv::LanePlacement::Scroll);

    const ImVec2 traceMin(traceLeft, visibleTop);
    const ImVec2 traceMax(traceLeft + traceWidth, visibleBottom);
    fillPixels(draw, traceMin, traceMax, background);

    for (const ShownLane& shown : shownLanes) {
      if (shown.lane.key.kind != wv::LaneKind::Channel) {
        continue;
      }
      const ImVec2 laneMin(traceLeft, shown.visibleTop());
      const ImVec2 laneMax(traceLeft + traceWidth, shown.visibleBottom());
      fillPixels(draw, laneMin, laneMax, color32(wv::laneBackgroundFor(laneChoice(shown.lane.source), dark)));
    }

    BITOSCOPE_DRAW_PART_NEXT(waveformPart, Headers);
    const float pinWidth = triggerIconSize + triggerFramePadding.x * 2.0f;
    const float pinHeight = triggerIconSize + triggerFramePadding.y * 2.0f;
    const float pinLeft = (std::max)(origin.x, traceLeft - style.ItemSpacing.x - pinWidth);
    const float nameRight = pinLeft - style.ItemSpacing.x * 0.5f;
    const float chipHeight = std::round(em * 1.15f);
    const float chipRounding = std::round(chipHeight * 0.3f);
    float chipWidth = chipHeight;
    for (const std::uint32_t device : waveform->sources.laneDevice()) {
      chipWidth = (std::max)(chipWidth, std::ceil(ImGui::CalcTextSize(std::to_string(device).c_str()).x + em * 0.5f));
    }
    const auto laneDeviceOf = [&](std::uint32_t lane) {
      return lane < waveform->sources.laneDevice().size() ? waveform->sources.laneDevice()[lane] : lane;
    };
    const auto laneShownName = [&](std::uint32_t lane) {
      return gc::shownChannelName(ui.channelUserNames, ui.channelNames, laneDeviceOf(lane));
    };
    const auto drawColorBox = [&](ImVec2 min, float width, float textX, const wv::ColorChoice& choice,
                                  const std::string& text) {
      const wv::SelectorButtonColors colors = wv::selectorButtonColors(choice, dark);
      const ImVec2 max(min.x + width, min.y + chipHeight);
      draw->AddRectFilled(min, max, color32(colors.fill), chipRounding);
      if (colors.outlined) {
        addEdgeCentredOutline(draw, min, max, color32(colors.outline), chipRounding, (std::max)(1.0f, std::round(scale)));
      }
      draw->PushClipRect(min, ImVec2(max.x - chipRounding * 0.5f, max.y), true);
      draw->AddText(ImVec2(min.x + textX, gui_lane_header::capCentredTextY(min.y, chipHeight)),
                    color32(colors.selectedText), text.c_str());
      draw->PopClipRect();
    };
    const auto drawChip = [&](ImVec2 min, float width, float textX, std::uint32_t lane, const std::string& text) {
      drawColorBox(min, width, textX, laneChoice(lane), text);
    };
    const auto drawChannelChip = [&](ImVec2 min, std::uint32_t lane) {
      const std::string number = std::to_string(laneDeviceOf(lane));
      drawChip(min, chipWidth, gui_lane_header::inkCentredTextX(number, 0.0f, chipWidth), lane, number);
    };
    const float nameGap = std::round(em * 0.25f);
    const float namePad = std::round(em * 0.3f);
    const auto drawChannelName = [&](ImVec2 min, std::uint32_t lane) {
      const float width = nameRight - min.x;
      if (width <= namePad * 2.0f) {
        return;
      }
      drawChip(min, width, namePad, lane, laneShownName(lane));
    };
    struct RowHeaderLine {
      std::string text;
      ImU32 color;
    };
    const auto decoderRowHeaderLine = [&](std::size_t d, const std::string& laneLabel, std::size_t slot) {
      const gd::DecoderLaneModel& laneRows = decoderLanes[d];
      const decode::AnnotationRow& row = laneRows.rows[slot];
      const gd::DecoderLaneState state = ui.decoderAnnotations.state(d);
      RowHeaderLine line{wv::decoderRowHeader(laneLabel, row.name, laneRows.rows.size()), textColor};
      if (slot != 0) return line;
      const gd::LaneProgress* progress = ui.decoderAnnotations.progress(d);
      const wv::LaneStatus status = wv::decoderLaneStatus(
        state == gd::DecoderLaneState::Decoding, state == gd::DecoderLaneState::Failed,
        ui.decoderAnnotations.drawn(d) != nullptr,
        progress != nullptr ? std::optional<int>(progress->percent) : std::nullopt,
        ui.decodes.laneCaptureMode(ui.running.has_value(), ui.running && gd::liveDecodingAllowed(*ui.running),
                                   ui.decoderAnnotations.runPositionOf(d)));
      switch (status.kind) {
        case wv::LaneStatusKind::Decoding:
          line.text = std::format("{} - {}", line.text, tr("decoding..."));
          line.color = mutedColor;
          break;
        case wv::LaneStatusKind::DecodingPercent:
          line.text = std::format("{} - {}", line.text, trf("decoding {}%", status.percent));
          line.color = mutedColor;
          break;
        case wv::LaneStatusKind::StartsWhenCaptureEnds:
          line.text = std::format("{} - {}", line.text, tr("decoding starts when capture ends"));
          line.color = mutedColor;
          break;
        case wv::LaneStatusKind::Failed:
          line.text = std::format("{}: {}", line.text, ui.decoderAnnotations.error(d));
          line.color = ImGui::GetColorU32(errorTextColor(dark));
          break;
        case wv::LaneStatusKind::None:
          break;
      }
      return line;
    };
    const auto opaqueChoice = [](const ImVec4& color) {
      const auto byte = [](float v) {
        return static_cast<std::uint32_t>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f));
      };
      return wv::ColorChoice::custom(0xFF000000u | byte(color.x) << 16 | byte(color.y) << 8 | byte(color.z));
    };
    const wv::ColorChoice neutralDecoderChoice = opaqueChoice(ImGui::ColorConvertU32ToFloat4(accent));
    const auto decoderChoice = [&](std::size_t d) { return ui.decoders[d].color().value_or(neutralDecoderChoice); };
    const auto decoderBarText = [&](std::size_t d) -> const std::string& { return ui.laneLabels.at(d).barName; };
    const auto decoderSizesOf = [&](std::size_t d, const std::string& laneLabel, double columnRight) {
      const gd::DecoderLaneModel& laneRows = decoderLanes[d];
      wv::DecoderHeaderSizes sizes;
      sizes.rowHeight = decoderPitch;
      sizes.em = em;
      sizes.indent = style.ItemSpacing.x;
      sizes.box = chipHeight;
      sizes.boxGap = nameGap;
      sizes.nameText = ImGui::CalcTextSize(decoderBarText(d).c_str()).x;
      sizes.namePadding = namePad;
      sizes.zoneGap = style.ItemSpacing.x;
      sizes.columnRight = columnRight;
      for (std::size_t slot = 0; slot < laneRows.rows.size(); ++slot) {
        sizes.widestRowText =
          (std::max)(sizes.widestRowText,
                     static_cast<double>(ImGui::CalcTextSize(decoderRowHeaderLine(d, laneLabel, slot).text.c_str()).x));
      }
      return sizes;
    };
    std::vector<std::optional<wv::DecoderHeaderSizes>> decoderHeaderSizes(decoderCount);
    const auto decoderSizesAt = [&](std::size_t d) -> const wv::DecoderHeaderSizes& {
      if (!decoderHeaderSizes[d]) {
        decoderHeaderSizes[d] =
          decoderSizesOf(d, ui.laneLabels.at(d).laneLabel, static_cast<double>(nameRight - origin.x));
      }
      return *decoderHeaderSizes[d];
    };
    const auto decoderHeaderOf = [&](std::size_t d) { return wv::decoderHeaderLayout(decoderSizesAt(d)); };
    const auto decoderRowPlaceOf = [&](std::size_t d) {
      const wv::DecoderHeaderSizes& sizes = decoderSizesAt(d);
      return wv::decoderRowPlace(sizes, wv::decoderHeaderLayout(sizes));
    };
    const auto failureTooltip = [&](std::size_t d) {
      ImGui::BeginTooltip();
      ImGui::PushTextWrapPos(em * 30.0f);
      ImGui::TextUnformatted(ui.decoderAnnotations.error(d).c_str());
      ImGui::PopTextWrapPos();
      ImGui::EndTooltip();
    };
    const auto fittedHeaderWidth = [&]() {
      const double itemSpacing = style.ItemSpacing.x;
      std::vector<wv::HeaderParts> headers;
      for (const ShownLane& shown : shownLanes) {
        if (shown.lane.key.kind == wv::LaneKind::Channel) {
          wv::HeaderParts parts;
          parts.indent = itemSpacing;
          parts.marker = chipWidth;
          parts.markerGap = nameGap;
          parts.text = ImGui::CalcTextSize(laneShownName(shown.lane.source).c_str()).x + 2.0f * namePad;
          parts.pin = pinWidth;
          parts.pinGap = itemSpacing * 0.5;
          parts.trailing = itemSpacing;
          headers.push_back(parts);
          continue;
        }
        const std::size_t d = shown.lane.source;
        const gd::DecoderLaneModel& laneRows = decoderLanes[d];
        const std::string& laneLabel = ui.laneLabels.at(d).laneLabel;
        std::vector<double> texts;
        for (std::size_t slot = 0; slot < laneRows.rows.size(); ++slot) {
          texts.push_back(ImGui::CalcTextSize(decoderRowHeaderLine(d, laneLabel, slot).text.c_str()).x);
        }
        wv::DecoderHeaderMetrics metrics;
        metrics.indent = itemSpacing;
        const wv::DecoderHeaderSizes wholeSizes = decoderSizesOf(d, laneLabel, 1.0e9);
        metrics.leftZone = wv::decoderHeaderLayout(wholeSizes).leftZone + wholeSizes.zoneGap;
        metrics.textGap = nameGap;
        metrics.pin = pinWidth;
        metrics.pinGap = itemSpacing * 0.5;
        metrics.trailing = itemSpacing;
        const std::vector<wv::HeaderParts> rowHeaders = wv::decoderLaneHeaders(texts, metrics);
        headers.insert(headers.end(), rowHeaders.begin(), rowHeaders.end());
      }
      const double fitted = wv::fitHeaderWidth(
        headers, 1.0, gui_prefs::laneNameWidthPixels(gui_prefs::kLaneNameWidthMinCentiEm, em, 0.0f), headerWidest);
      return static_cast<float>(std::ceil(fitted));
    };
    bool nameFieldDrawn = false;
    const wv::PinSide rememberedSide = gui_prefs::effectiveLanePinSide(ui.lanePinSide, lanePinSide_);
    bool laneNameHovered = false;
    bool pinMenuOpened = false;
    const auto headerControl = [&](const ShownLane& shown, bool pressed) {
      return gui_lane_header::headerControl(waveform->laneDrag, shown.lane.key, shown.top, pressed);
    };
    const auto lanePinButton = [&](const ShownLane& shown, float y) {
      const bool pinned = shown.placement != wv::LanePlacement::Scroll;
      ImGui::SetCursorScreenPos(ImVec2(pinLeft, y));
      ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, triggerFramePadding);
      if (!pinned) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
      }
      const std::string id = laneWidgetId("pin", shown.lane.key);
      if (headerControl(shown, iconButtonSized(id.c_str(), Icon::Pin, triggerIconPixels,
                                               pinTooltip(shown.placement, rememberedSide), false, pinned))) {
        applyLaneAction(ui, wv::clickPin(ui.laneOrder, shown.lane.key, rememberedSide));
      }
      if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        waveform->laneMenuKey = shown.lane.key;
        gui_popup::open("lane_pin_menu", popupOwner);
        pinMenuOpened = true;
      }
      if (!pinned) {
        ImGui::PopStyleColor();
      }
      ImGui::PopStyleVar();
    };
    const auto laneNameHandle = [&](const ShownLane& shown) {
      const ImVec2 min(origin.x, shown.top);
      const ImVec2 max((std::max)(origin.x + 1.0f, traceLeft - kLaneDividerWidth * scale), shown.bottom());
      if (max.x - min.x < 1.0f || max.y - min.y < 1.0f) {
        return false;
      }
      const std::string id = laneWidgetId("name", shown.lane.key);
      if (gui_lane_header::dragHandle(waveform->laneDrag, shown.lane.key, id.c_str(), min, max, shown.top)) {
        laneNameHovered = true;
      }
      return true;
    };

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, triggerFramePadding);
    for (const ShownLane& shown : shownLanes) {
      if (shown.lane.key.kind != wv::LaneKind::Channel) {
        continue;
      }
      const std::uint32_t c = shown.lane.source;
      const float top = shown.top;
      const LaneClip headerClip(ImVec2(origin.x, shown.clipTop), ImVec2(traceLeft, shown.clipBottom));
      const float headerBlockTop = top + (pitch - headerContentHeight) * 0.5f;
      const float x = origin.x + style.ItemSpacing.x;
      const std::uint32_t device = laneDeviceOf(c);
      const ImVec2 chipMin(x, headerBlockTop + (em - chipHeight) * 0.5f);
      const float nameX = x + chipWidth + nameGap;
      if (laneNameHandle(shown) && gui_lane_header::nameClicked(waveform->laneDrag, ImVec2(nameX, chipMin.y),
                                                                ImVec2(nameRight, chipMin.y + chipHeight))) {
        gui_lane_header::beginNameEdit(waveform->nameEdit, shown.lane.key, laneShownName(c));
      }
      const wv::ColorChoice& choice = laneChoice(c);
      drawChannelChip(chipMin, c);
      ImGui::SetCursorScreenPos(chipMin);
      const std::string swatchId = std::format("##color_swatch_{}", c);
      if (headerControl(shown, ImGui::InvisibleButton(swatchId.c_str(), ImVec2(chipWidth, chipHeight)))) {
        waveform->colorPickerChannel = device;
        loadColorDraft(waveform->colorDraft, choice);
        gui_popup::open("channel_color_picker", popupOwner);
      }
      if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", tr("Choose channel's color"));
      }
      const bool editingName = waveform->nameEdit.lane == shown.lane.key;
      if (!editingName) {
        drawChannelName(ImVec2(nameX, chipMin.y), c);
      }
      lanePinButton(shown, headerBlockTop + (em - pinHeight) * 0.5f);

      const float iconRowY = headerBlockTop + em + headerRowGap;
      float bx = x;
      trigger::MatchType* channelTrigger = laneTrigger(c);
      ImGui::BeginDisabled(channelTrigger == nullptr || ui.capture.running());
      for (const TriggerButtonSpec& button : kTriggerButtons) {
        ImGui::SetCursorScreenPos(ImVec2(bx, iconRowY));
        const bool active = channelTrigger != nullptr && *channelTrigger == button.type;
        ImGui::PushStyleColor(ImGuiCol_Button, active ? ImGui::GetStyleColorVec4(ImGuiCol_Header)
                                                      : ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        const std::string id = std::format("##trig{}_{}", c, static_cast<int>(button.type));
        if (headerControl(shown, iconButtonSized(id.c_str(), button.icon, triggerIconPixels,
                                                 triggerButtonTooltip(button.type))) &&
            channelTrigger != nullptr) {
          *channelTrigger = wv::toggleChannelTrigger(*channelTrigger, button.type);
        }
        ImGui::PopStyleColor();
        bx += triggerIconSize + triggerFramePadding.x * 2.0f + triggerGap;
      }
      ImGui::EndDisabled();
      if (editingName) {
        nameFieldDrawn = true;
        const std::string id = laneWidgetId("name_edit", shown.lane.key);
        const ImVec2 at(nameX + namePad - triggerFramePadding.x,
                        gui_lane_header::capCentredTextY(chipMin.y, chipHeight) - triggerFramePadding.y);
        if (const auto typed = gui_lane_header::nameField(waveform->nameEdit, id.c_str(), at,
                                                          (std::max)(em, nameRight - at.x))) {
          if (gc::renameChannel(ui.channelUserNames, ui.channelNames, device, *typed)) {
            ui.capture.markViewEdited();
          }
        }
      }
    }
    ImGui::PopStyleVar();

    const ImVec4 accentRgba = ImGui::ColorConvertU32ToFloat4(accent);
    const auto channel8 = [](float v) { return static_cast<std::uint8_t>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); };
    const wv::Rgb8 neutralFill{channel8(accentRgba.x), channel8(accentRgba.y), channel8(accentRgba.z)};
    for (const ShownLane& shown : shownLanes) {
      if (shown.lane.key.kind != wv::LaneKind::Decoder) {
        continue;
      }
      const std::size_t d = shown.lane.source;
      gd::DecoderConfig& config = ui.decoders[d];
      ImGui::PushID(laneWidgetId("rows", shown.lane.key).c_str());
      const wv::DecoderHeaderLayout header = decoderHeaderOf(d);
      {
        const LaneClip pairClip(ImVec2(origin.x, shown.clipTop), ImVec2(nameRight, shown.clipBottom));
        ImGui::SetCursorScreenPos(ImVec2(origin.x + static_cast<float>(header.square.x),
                                         shown.top + static_cast<float>(header.square.y)));
        if (headerControl(shown, ImGui::InvisibleButton("##decoder_color",
                                                        ImVec2(static_cast<float>(header.square.width),
                                                               static_cast<float>(header.square.height))))) {
          waveform->colorPickerDecoder = config.id();
          loadColorDraft(waveform->colorDraft, config.color());
          gui_popup::open("decoder_color_picker", popupOwner);
        }
        if (ImGui::IsItemHovered()) {
          ImGui::SetTooltip("%s", tr("Choose decoder color"));
        }
        if (waveform->nameEdit.lane != shown.lane.key) {
          ImGui::SetCursorScreenPos(ImVec2(origin.x + static_cast<float>(header.bar.x),
                                           shown.top + static_cast<float>(header.bar.y)));
          if (headerControl(shown, ImGui::InvisibleButton("##decoder_name",
                                                          ImVec2(static_cast<float>(header.bar.width),
                                                                 static_cast<float>(header.bar.height))))) {
            gui_lane_header::beginNameEdit(waveform->nameEdit, shown.lane.key, ui.laneLabels.at(d).barName);
            waveform->decoderNameRefusal.reset();
          }
          if (ImGui::IsItemHovered() && ui.decoderAnnotations.state(d) == gd::DecoderLaneState::Failed) {
            failureTooltip(d);
          }
        }
      }
      ImGui::PopID();
    }

    BITOSCOPE_DRAW_PART_NEXT(waveformPart, Prologue);
    const auto verticalLine = [&](float x, float top, float bottom, ImU32 color, float thickness) {
      draw->AddLine(ImVec2(x, top), ImVec2(x, bottom), color, thickness);
    };

    {
      const float dividerWidth = kLaneDividerWidth * scale;
      ImGui::SetCursorScreenPos(ImVec2(traceLeft - dividerWidth, visibleTop));
      ImGui::InvisibleButton("laneNameDivider", ImVec2(dividerWidth, (std::max)(1.0f, visibleBottom - visibleTop)));
      const bool dividerActive = ImGui::IsItemActive();
      const bool dividerHovered = ImGui::IsItemHovered() && !waveform->laneDrag.dragged;
      if (dividerHovered || dividerActive) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        const float x = std::floor(traceLeft - dividerWidth * 0.5f);
        verticalLine(x + 0.5f, visibleTop + 0.5f, visibleBottom + 0.5f,
                     ImGui::GetColorU32(dividerActive ? ImGuiCol_SeparatorActive : ImGuiCol_SeparatorHovered),
                     (std::max)(1.0f, scale));
      }
      if (ImGui::IsItemActivated() && ImGui::IsMousePosValid()) {
        laneDividerPressX_ = io.MousePos.x;
        laneDividerPressCentiEm_ = laneNameWidthCentiEm_;
      }
      if (dividerActive && ImGui::IsMousePosValid()) {
        // WHY: Width moves by pointer travel from the press: re-deriving it from the pointer shifted it on first click.
        laneNameWidthCentiEm_ = gui_prefs::laneNameWidthDragged(laneDividerPressCentiEm_, em, laneDividerPressX_,
                                                                io.MousePos.x, headerWidest);
      }
      if (dividerHovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && ImGui::IsMousePosValid()) {
        const float fitted = fittedHeaderWidth();
        laneNameWidthCentiEm_ = gui_prefs::laneNameWidthFromPixels(fitted, em, headerWidest);
        laneDividerPressX_ = io.MousePos.x;
        laneDividerPressCentiEm_ = laneNameWidthCentiEm_;
      }
    }

    ImGui::SetCursorScreenPos(traceMin);
    ImGui::InvisibleButton("traces", ImVec2(traceWidth, (std::max)(1.0f, visibleBottom - visibleTop)));
    tracesHovered = ImGui::IsItemHovered();
    tracesActive = ImGui::IsItemActive();
    const double mouseX = static_cast<double>(io.MousePos.x - traceLeft);

    const float lineGrab = kCursorGrab * scale;
    const bool pointerValid = ImGui::IsMousePosValid();
    if (ImGui::IsItemActivated() && pointerValid) {
      waveform->grabbedSample = viewport.pixelToSample(mouseX);
      waveform->tracesPreviousPressDragged = waveform->tracesPressDragged;
      waveform->tracesPressDragged = false;
      waveform->panScrollAtPress = static_cast<double>(ImGui::GetScrollY());
      waveform->panPressY = static_cast<double>(io.MousePos.y);
      const wv::GrabTarget grabbed =
        wv::grabAtPixel(waveform->markers, cursorsForGrab, cursorsGrabbable, viewport, mouseX, lineGrab);
      waveform->grab.begin(
        wv::tracesPressTarget(grabbed, cursorsGrabbable, cursorsGrabbable && overWidthLabelAt(cursorsForGrab)), mouseX,
        cursorsForGrab);
      waveform->tracesPressClicks = ImGui::GetMouseClickedCount(ImGuiMouseButton_Left);
      waveform->tracesPressOnLine = waveform->grab.active();
    }
    const wv::PointerGrab pressGrab = waveform->grab;
    if (tracesActive && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
      waveform->tracesPressDragged = true;
      if (pressGrab.marker()) {
        waveform->selectedMarker = pressGrab.marker();
      }
    }
    if (tracesActive && pointerValid) {
      const wv::DragOutcome dragged = waveform->grab.dragTo(waveform->markers, waveform->cursors, viewport, mouseX,
                                                            total, waveform->tracesPressDragged);
      if (dragged == wv::DragOutcome::Moved) {
        ui.capture.markViewEdited();
      } else if (dragged == wv::DragOutcome::NotHeld) {
        const double moved = mouseX - viewport.sampleToPixel(waveform->grabbedSample);
        if (moved != 0.0) {
          wv::applyDrag(viewport, moved, viewTotal);
        }
        const double maxScrollY = regions.maxScroll;
        ImGui::SetScrollY(static_cast<float>(wv::dragScrollY(
          waveform->panScrollAtPress, waveform->panPressY, static_cast<double>(io.MousePos.y), maxScrollY)));
      }
    }
    // WHY: Reset shared drag fields only when BOTH ruler and traces are inactive, or one clobbers the other's this frame.
    if (!rulerActive && !tracesActive) {
      waveform->grab.release();
    }
    if (ImGui::IsItemDeactivated() && pressGrab.selectsMarkerOnRelease(waveform->tracesPressDragged)) {
      waveform->selectedMarker = pressGrab.marker();
    }
    if (ImGui::IsItemDeactivated() && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
      const float dragThreshold = io.MouseDragThreshold;
      const bool releaseDragged =
        waveform->tracesPressDragged ||
        io.MouseDragMaxDistanceSqr[ImGuiMouseButton_Left] >= dragThreshold * dragThreshold;
      const wv::DoubleClickPress press{wv::PointerRegion::Traces,
                                       viewport.sampleToPixel(waveform->grabbedSample),
                                       waveform->tracesPressClicks,
                                       releaseDragged,
                                       waveform->tracesPreviousPressDragged,
                                       waveform->tracesPressOnLine};
      if (const auto at = wv::markerForDoubleClick(press, viewport, total)) {
        waveform->markers.add(*at);
        ui.capture.markViewEdited();
      }
    }
    const wv::GrabTarget hoverGrab =
      wv::grabAtPixel(waveform->markers, cursorsForGrab, cursorsGrabbable, viewport, mouseX, lineGrab);
    const bool overLabel = tracesHovered && pointerValid && cursorsGrabbable && overWidthLabelAt(cursorsForGrab);
    const wv::PointerShape tracesShape = wv::tracesPointerShape(
      waveform->grab, tracesHovered && (hoverGrab.kind != wv::GrabKind::None || overLabel),
      tracesActive && ImGui::IsMouseDragging(ImGuiMouseButton_Left));
    if (tracesShape == wv::PointerShape::ResizeEW) {
      ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    } else if (tracesShape == wv::PointerShape::Hand) {
      ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }
    const bool headersHovered = ImGui::IsWindowHovered() && io.MousePos.x < traceLeft;
    if (headersHovered && io.MouseWheel != 0.0f && lanesScroll) {
      ImGui::SetScrollY(ImGui::GetScrollY() - io.MouseWheel * pitch);
    }
    if ((tracesHovered || rulerHovered) && (io.MouseWheel != 0.0f || io.MouseWheelH != 0.0f)) {
      wv::applyWheel(viewport, wv::wheelAction(io.MouseWheel, io.MouseWheelH, modifiers(io)), mouseX, viewTotal);
    }
    const bool keysFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    if ((keysFocused || tracesHovered || rulerHovered) && !io.WantTextInput && !ImGui::IsAnyItemActive() &&
        !waveform->find.ownsKeys()) {
      const double anchor = (tracesHovered || rulerHovered) ? static_cast<double>(io.MousePos.x - traceLeft)
                                                            : viewport.widthPixels() * 0.5;
      for (const KeyBinding& key : kKeys) {
        if (ImGui::IsKeyPressed(key.imgui, true)) {
          const wv::ViewCommand command = wv::commandForKey(key.view, modifiers(io));
          if (command != wv::ViewCommand::None) {
            waveformCommand(ui, command, anchor);
          }
        }
      }
    }

    ticks = wv::buildTimeRuler(
        viewport, kRulerLabelSpacing * scale, 5, [](const std::string& label) {
          return static_cast<double>(ImGui::CalcTextSize(label.c_str()).x);
        });

    const auto drawGridLines = [&](double top, double bottom) {
      for (const wv::RulerTick& tick : ticks) {
        if (tick.major) {
          verticalLine(columnX(static_cast<std::uint64_t>(tick.sample)), static_cast<float>(top),
                       static_cast<float>(bottom), gridColor, 1.0f);
        }
      }
    };
    std::vector<wv::YInterval> decoderGridBands;
    for (const ShownLane& shown : shownLanes) {
      if (shown.lane.key.kind == wv::LaneKind::Decoder) {
        decoderGridBands.push_back({static_cast<double>(shown.visibleTop()), static_cast<double>(shown.visibleBottom())});
      }
    }

    // WHY: Draw decoder lanes AFTER the drag and wheel: before them the lanes showed last frame's viewport and lagged.
    std::function<void(const ShownLane&, bool)> drawDecoderLane;
    // WHY: Commit a typed decoder name after every lane is drawn: a rename decodes again, which drawn lanes must not see.
    struct PendingDecoderRename {
      std::size_t decoder = 0;
      std::string typed;
    };
    std::optional<PendingDecoderRename> pendingDecoderRename;
    bool rowFormatMenuOpened = false;
    const bool trimmable = waveform->sources.shows() == gwf::WaveformSourceChain::Shows::Kept;
    if (decoderCount > 0) {
      drawDecoderLane = [&](const ShownLane& shown, bool floating) {
        BITOSCOPE_DRAW_PART(lanePart, frameProbe_, DecoderLanes);
        const wv::Viewport annotationColumns = columns();
        const double mouseColumnD =
          static_cast<double>(io.MousePos.x) * framebufferScale - std::round(traceLeft * framebufferScale);

        const std::size_t d = shown.lane.source;
        const float top = shown.top;
        const float laneTop = shown.visibleTop();
        const float laneBottom = shown.visibleBottom();
        const gd::DecoderLaneModel& laneRows = decoderLanes[d];
        const std::vector<decode::AnnotationRow>& rows = laneRows.rows;
        const std::size_t rowCount = rows.size();
        const gd::DecoderConfig& config = ui.decoders[d];
        std::vector<float> rowTops(rowCount);
        for (std::size_t slot = 0; slot < rowCount; ++slot) {
          rowTops[slot] = top + static_cast<float>(slot) * decoderPitch;
        }
        const auto rowBandTop = [&](std::size_t slot) { return rowTops[slot]; };
        const auto rowBandBottom = [&](std::size_t slot) {
          return rowTops[slot] + decoderPitch;
        };
        const LaneClip laneClip(ImVec2(origin.x, shown.clipTop), ImVec2(traceLeft + traceWidth, shown.clipBottom));
        for (std::size_t slot = 0; slot < rowCount; ++slot) {
          fillPixels(draw, ImVec2(origin.x, rowBandTop(slot)), ImVec2(traceLeft + traceWidth, rowBandBottom(slot)),
                              withAlpha(mutedColor, dark ? 0.06f : 0.05f));
          if (config.color()) {
            fillPixels(draw, ImVec2(traceLeft, rowBandTop(slot)), ImVec2(traceLeft + traceWidth, rowBandBottom(slot)),
                                color32(wv::laneBackgroundFor(*config.color(), dark)));
          }
        }
        if (!floating) {
          draw->PushClipRect(ImVec2(traceLeft, laneTop), ImVec2(traceLeft + traceWidth, laneBottom), true);
          drawGridLines(static_cast<double>(laneTop), static_cast<double>(laneBottom));
          draw->PopClipRect();
        }
        for (std::size_t slot = 1; slot < rowCount; ++slot) {
          draw->AddLine(pixelCentre(origin.x, rowBandTop(slot)), pixelCentre(traceLeft + traceWidth, rowBandTop(slot)),
                        withAlpha(mutedColor, 0.3f), lineWidth);
        }

        const gd::DecoderLaneState state = ui.decoderAnnotations.state(d);
        const std::string& laneLabel = ui.laneLabels.at(d).laneLabel;
        const float firstBand = decoderPitch;
        if (!floating) {
          laneNameHandle(shown);
          lanePinButton(shown, top + (firstBand - pinHeight) * 0.5f);
        }
        const wv::DecoderHeaderLayout header = decoderHeaderOf(d);
        const wv::DecoderRowPlace place = decoderRowPlaceOf(d);
        const bool editingName = !floating && waveform->nameEdit.lane == shown.lane.key;
        const ImVec2 barMin(origin.x + static_cast<float>(header.bar.x), top + static_cast<float>(header.bar.y));
        const float barWidth = static_cast<float>(header.bar.width);
        draw->PushClipRect(ImVec2(origin.x, top), ImVec2(nameRight, top + firstBand), true);
        drawColorBox(ImVec2(origin.x + static_cast<float>(header.square.x), top + static_cast<float>(header.square.y)),
                     static_cast<float>(header.square.width), 0.0f, decoderChoice(d), std::string());
        if (!editingName) {
          drawColorBox(barMin, barWidth, namePad,
                       state == gd::DecoderLaneState::Failed ? opaqueChoice(errorTextColor(dark)) : decoderChoice(d),
                       decoderBarText(d));
        }
        draw->PopClipRect();
        const ImVec2 fieldAt(barMin.x + namePad - triggerFramePadding.x,
                             gui_lane_header::capCentredTextY(barMin.y, chipHeight) - triggerFramePadding.y);
        if (const std::optional<wv::HeaderSpan> fieldSpan =
              editingName ? wv::decoderNameFieldSpan(header, static_cast<double>(fieldAt.x - origin.x),
                                                     static_cast<double>(nameRight - origin.x))
                          : std::nullopt) {
          nameFieldDrawn = true;
          ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, triggerFramePadding);
          const std::string id = laneWidgetId("name_edit", shown.lane.key);
          if (auto typed = gui_lane_header::nameField(waveform->nameEdit, id.c_str(), fieldAt,
                                                      static_cast<float>(fieldSpan->right - fieldSpan->left))) {
            pendingDecoderRename = PendingDecoderRename{d, std::move(*typed)};
          }
          ImGui::PopStyleVar();
        }
        if (!floating && waveform->decoderNameRefusal && waveform->decoderNameRefusal->decoderId == config.id()) {
          ImGui::SetNextWindowPos(ImVec2(barMin.x, barMin.y + chipHeight + style.ItemInnerSpacing.y));
          const std::string window = std::format("##decoder_name_refusal_{}", sessionId);
          ImGui::Begin(window.c_str(), nullptr,
                       ImGuiWindowFlags_Tooltip | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoDecoration |
                         ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoDocking);
          ImGui::PushStyleColor(ImGuiCol_Text, errorTextColor(dark));
          ImGui::PushTextWrapPos(em * 30.0f);
          ImGui::TextUnformatted(waveform->decoderNameRefusal->message.c_str());
          ImGui::PopTextWrapPos();
          ImGui::PopStyleColor();
          ImGui::End();
        }
        if (state == gd::DecoderLaneState::Failed) {
          const ImVec2 headerMin(origin.x, top);
          const ImVec2 headerMax((std::max)(origin.x + 1.0f, traceLeft - kLaneDividerWidth * scale), top + firstBand);
          const bool overFreeHeader =
            floating ? !ImGui::IsAnyItemHovered()
                     : ImGui::GetHoveredID() == ImGui::GetID(laneWidgetId("name", shown.lane.key).c_str());
          if (overFreeHeader && ImGui::GetActiveID() == 0 &&
              !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel) &&
              ImGui::IsMouseHoveringRect(headerMin, headerMax)) {
            failureTooltip(d);
          }
        }
        for (std::size_t slot = 0; slot < rowCount; ++slot) {
          const RowHeaderLine headerLine = decoderRowHeaderLine(d, laneLabel, slot);
          draw->PushClipRect(ImVec2(origin.x, rowBandTop(slot)), ImVec2(nameRight, rowBandBottom(slot)), true);
          draw->AddText(ImVec2(origin.x + static_cast<float>(place.textX), rowBandTop(slot) + (decoderPitch - em) * 0.5f),
                        headerLine.color, headerLine.text.c_str());
          draw->PopClipRect();
        }

        const gd::ShownResult* drawnResult = laneRows.shown;
        if (drawnResult == nullptr) return;

        const gd::DecodedAnnotations* decoded = drawnResult->annotations.get();
        const wv::AnnotationGroups noGroups;
        const wv::AnnotationGroups& groups = decoded != nullptr ? decoded->groups() : noGroups;
        const gd::AnnotationTextSource laneText(drawnResult, config, hexStyle_, ui.laneLabels.at(d).rowSources,
                                                [](std::string_view s) {
                                                  return ImGui::CalcTextSize(s.data(), s.data() + s.size()).x;
                                                });
        const wv::DerivedRows& derived = laneRows.derivation.rows;
        const double laneFirstSample = annotationColumns.firstVisibleSample();
        const double laneSamplesPerPixel = annotationColumns.samplesPerPixel();
        const gd::LaneProgress* progress = ui.decoderAnnotations.progress(d);
        gd::LanePictureSources pictureSources;
        pictureSources.lane = &laneRows;
        pictureSources.config = &config;
        pictureSources.labels = &ui.laneLabels.at(d);
        pictureSources.hexStyle = hexStyle_;
        pictureSources.captureSerial = ui.laneViewSerial;
        pictureSources.fontSerial = fontSerial_;
        pictureSources.watermark = progress != nullptr ? progress->watermark : 0;
        pictureSources.columns = annotationColumns;
        pictureSources.columnCount = columnCount;
        pictureSources.geometry.traceLeft = traceLeft;
        pictureSources.geometry.traceRight = traceLeft + traceWidth;
        pictureSources.geometry.rowTops = rowTops;
        pictureSources.style.em = em;
        pictureSources.style.decoderPitch = decoderPitch;
        pictureSources.style.lineWidth = lineWidth;
        pictureSources.style.framebufferScale = framebufferScale;
        pictureSources.style.dark = dark;
        pictureSources.style.neutralFill = neutralFill;
        pictureSources.style.ink = color32(wv::annotationTextColor(dark));
        pictureSources.style.hatchFill = withAlpha(mutedColor, dark ? 0.16f : 0.12f);
        pictureSources.style.hatchStripe = withAlpha(mutedColor, dark ? 0.45f : 0.35f);
        pictureSources.pack = &packColor;
        pictureSources.text = &laneText;
        if (ui.lanePictures.size() != decoderCount) ui.lanePictures.resize(decoderCount);
        const wv::LanePicture& picture = ui.lanePictures[d].picture(pictureSources);
        const bool summarized = picture.summarized;
        const std::vector<wv::RowLayout>& rowLayouts = picture.rowLayouts;
        const std::optional<wv::HatchedColumns>& hatched = picture.hatched;
        draw->PushClipRect(ImVec2(traceLeft, laneTop), ImVec2(traceLeft + traceWidth, laneBottom), true);
        emitLanePicture(draw, picture);
        draw->PopClipRect();
        if (!floating) {
          FindMarkLane marks;
          marks.decoder = static_cast<std::uint32_t>(d);
          marks.rows = rows;
          marks.top = top;
          marks.rowPitch = decoderPitch;
          marks.clipMin = ImVec2(traceLeft, laneTop);
          marks.clipMax = ImVec2(traceLeft + traceWidth, laneBottom);
          marks.firstSample = static_cast<std::uint64_t>((std::max)(0.0, std::floor(laneFirstSample)));
          marks.endSample = static_cast<std::uint64_t>(
            (std::max)(0.0, std::ceil(annotationColumns.pixelToSample(static_cast<double>(columnCount))) + 1.0));
          marks.samplesPerPixel = laneSamplesPerPixel * static_cast<double>(framebufferScale);
          marks.sampleX = [&](std::uint64_t sample) { return columnX(sample); };
          marks.color = selectionColor;
          marks.frameWidth = selectionFrameWidth;
          marks.minWidth = 2.0f * lineWidth;
          drawFindMarks(draw, waveform->find, marks);
        }
        const std::optional<std::size_t> hoverSlot = wv::decoderRowAtY(io.MousePos.y, top, decoderPitch, rowCount);
        if (!floating && tracesHovered && !tracesActive && !laneDragging && ImGui::IsMousePosValid() &&
            io.MousePos.y >= laneTop && io.MousePos.y < laneBottom && hoverSlot && laneRows.takesFormat[*hoverSlot] &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
          waveform->rowFormatTarget = std::pair{config.id(), rows[*hoverSlot].id};
          waveform->trimPoint =
            trimmable ? std::optional<std::uint64_t>(wv::cursorSampleAtPixel(viewport, mouseX, total)) : std::nullopt;
          rowFormatMenuOpened = true;
          gui_popup::open("row_format_menu", popupOwner);
        }
        if (tracesHovered && !tracesActive && !laneDragging && ImGui::IsMousePosValid() && io.MousePos.y >= laneTop &&
            io.MousePos.y < laneBottom && hoverSlot && mouseColumnD >= 0.0 &&
            mouseColumnD < static_cast<double>(columnCount) && wv::laneColumnHittable(hatched, mouseColumnD)) {
          std::optional<decode::Annotation> hovered;
          if (summarized) {
            hovered = wv::laneRowAnnotationAt(groups, derived, rows[*hoverSlot].id, laneRows.visibility,
                                              laneFirstSample, laneSamplesPerPixel, columnCount,
                                              static_cast<std::uint32_t>(mouseColumnD));
          } else {
            const wv::RowLayout& layout = rowLayouts[*hoverSlot];
            const auto at = wv::rowMarkAtColumn(layout.marks, mouseColumnD);
            if (at) hovered = layout.annotations[*at];
          }
          if (hovered) {
            const decode::Annotation& a = *hovered;
            const std::string list =
              laneText.listsWords(a) ? laneText.wordList(a, kTooltipWordListChars) : std::string();
            if (list.empty()) {
              const gd::ValueBytes valueBytes = showByteValues_
                                                  ? gd::bytesFrom(*drawnResult, rows[*hoverSlot].id, a,
                                                                  gd::kMaxValueBytes)
                                                  : gd::ValueBytes{};
              const std::vector<gd::ByteValueLine> valueLines = gd::byteValueLines(valueBytes.span());
              if (valueLines.empty()) {
                ImGui::SetTooltip("%s", laneText.text(a).c_str());
              } else {
                ImGui::BeginTooltip();
                ImGui::TextUnformatted(laneText.text(a).c_str());
                drawByteValueLines(valueLines, em);
                ImGui::EndTooltip();
              }
            } else {
              ImGui::BeginTooltip();
              ImGui::PushTextWrapPos(em * 30.0f);
              ImGui::TextUnformatted(list.c_str());
              ImGui::PopTextWrapPos();
              ImGui::EndTooltip();
            }
          }
        }
        if (!floating && hoverSlot && ImGui::GetActiveID() == 0 &&
            !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel) &&
            ImGui::GetHoveredID() == ImGui::GetID(laneWidgetId("name", shown.lane.key).c_str()) &&
            io.MousePos.x < traceLeft) {
          const std::string* note = gd::derivationNote(laneRows.derivation, rows[*hoverSlot].id);
          const std::string& failure = *hoverSlot < rowLayouts.size() ? rowLayouts[*hoverSlot].failure : std::string();
          const std::string& reason = note != nullptr ? *note : failure;
          if (!reason.empty()) {
            ImGui::BeginTooltip();
            ImGui::PushTextWrapPos(em * 30.0f);
            ImGui::TextUnformatted(trf("Not drawn: {}", reason).c_str());
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
          }
        }
      };
      for (const ShownLane& shown : shownLanes) {
        if (shown.lane.key.kind == wv::LaneKind::Decoder) {
          drawDecoderLane(shown, false);
        }
      }
    }
    if (trimmable && !rowFormatMenuOpened && tracesHovered && !tracesActive && !laneDragging && pointerValid &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
      waveform->trimPoint = wv::cursorSampleAtPixel(viewport, mouseX, total);
      waveform->trimHovered.reset();
      gui_popup::open("trace_menu", popupOwner);
    }
    if (waveform->nameEdit.lane && !nameFieldDrawn) {
      waveform->nameEdit = {};
    }
    if (waveform->decoderNameRefusal && ImGui::GetFrameCount() > waveform->decoderNameRefusal->frame &&
        (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right))) {
      waveform->decoderNameRefusal.reset();
    }
    if (pendingDecoderRename && pendingDecoderRename->decoder < ui.decoders.size()) {
      const std::string oldId = ui.decoders[pendingDecoderRename->decoder].id();
      const auto committed =
        gd::commitDecoderName(ui.decoders, pendingDecoderRename->decoder, pendingDecoderRename->typed, decoderRegistry());
      if (!committed) {
        waveform->decoderNameRefusal = SessionWaveform::DecoderNameRefusal{oldId, committed.error(), ImGui::GetFrameCount()};
        logger_.logf(log::LogLevel::warning, "decode", "decoder '{}' not renamed: {}",
                     gd::displayInstanceName(oldId, ui.decoders[pendingDecoderRename->decoder].decoderName()),
                     committed.error());
      } else if (*committed) {
        DecoderSettingsChange change;
        change.setup = true;
        applyDecoderSettingsChange(sessionId, ui, change);
      }
    }

    BITOSCOPE_DRAW_PART_NEXT(waveformPart, Columns);
    {
      const wv::Viewport columnView = columns();
      // WHY: Only the cache's row work runs off this thread (pure, no Dear ImGui state); rows are uploaded here.
      const gwf::ColumnUpdate drawn =
        waveform->columns.update(source, wv::channelRowsToRender(shownLanes, allLanes, waveform->laneDrag.dragged),
                                 columnView, renderPool_.get());
      for (const std::string& failure : drawn.laneFailures) {
        logger_.logf(log::LogLevel::error, "gui",
                     "Waveform lane failed, its channels drawn on this thread instead: {}", failure);
      }
      if (drawn.anyError) {
        for (std::uint32_t c = 0; c < drawn.errors.size(); ++c) {
          if (!drawn.errors[c].empty()) {
            logger_.logf(log::LogLevel::error, "gui", "Channel {} could not be drawn: {}", c,
                         drawn.errors[c]);
          }
        }
      }
      if (drawn.rowsMade) {
        waveform->passUploads.rowsMade();
      }
    }
    BITOSCOPE_DRAW_PART_NEXT(waveformPart, Upload);
    std::array<std::vector<const ShownLane*>, kPlacements.size()> regionChannelLanes;
    for (const ShownLane& shown : shownLanes) {
      if (shown.lane.key.kind == wv::LaneKind::Channel) {
        regionChannelLanes[static_cast<std::size_t>(shown.placement)].push_back(&shown);
      }
    }
    if (renderer != nullptr) {
      for (const wv::LanePlacement placement : kPlacements) {
        const std::uint64_t passView = regionView(waveform->view, placement);
        if (!regionChannelLanes[static_cast<std::size_t>(placement)].empty() &&
            waveform->passUploads.needed(passView, renderer->holds(passView, columnCount, channels))) {
          renderer->upload(passView, columnCount, channels, waveform->columns.levels());
          waveform->passUploads.uploaded(passView);
        }
      }
    }

    BITOSCOPE_DRAW_PART_NEXT(waveformPart, DotsOverlays);
    draw->PushClipRect(traceMin, traceMax, true);
    for (const wv::YInterval& piece : wv::cutStrips({visibleTop + 0.5, visibleBottom + 0.5}, decoderGridBands)) {
      drawGridLines(piece.top, piece.bottom);
    }

    std::vector<std::pair<WaveformRenderer::Lane, const ShownLane*>> lanes;
    const auto passLane = [&](std::uint32_t c, float top) {
      const wv::LaneColors lc = wv::laneColors(toWaveformViewStyle(signalStyle_), laneChoice(c), dark);
      WaveformRenderer::Lane lane;
      lane.top = std::round(top + waveOffsets.top);
      lane.bottom = std::round(top + waveOffsets.bottom);
      lane.row = c;
      lane.highColor = color4(lc.high);
      lane.lowColor = color4(lc.low);
      lane.edgeColor = color4(lc.edge);
      lane.needsOutline = lc.outline;
      lane.outlineColor = color4(lc.outlineColor);
      lane.fillColor = lc.fill ? color4(lc.fill->color, lc.fill->alpha / 255.0f) : std::array<float, 4>{};
      return lane;
    };
    const auto paintDenseSpans = [&](const WaveformRenderer::Lane& lane) {
      if (lane.row >= waveform->columns.spans().size() || waveform->columns.spans()[lane.row].empty()) {
        return;
      }
      const wv::LaneColors lc = wv::laneColors(toWaveformViewStyle(signalStyle_), laneChoice(lane.row), dark);
      const auto edgeX = [&](double column) {
        return static_cast<float>(wv::overlayColumnX(traceLeft, framebufferScale, column));
      };
      for (const wv::DenseSpan& span : waveform->columns.spans()[lane.row]) {
        const wv::DenseSpanPaint paint = wv::denseSpanPaint(span, columnCount);
        for (std::size_t part = 0; part < paint.count; ++part) {
          const wv::DenseSpanInk& ink = paint.parts[part];
          const float x0 = edgeX(static_cast<double>(ink.firstColumn));
          const float x1 = edgeX(static_cast<double>(ink.lastColumn) + 1.0);
          // WHY: No rim around dense bars: a rim fattens them and fuses neighbours that are two columns apart.
          fillPixels(draw, ImVec2(x0, lane.top), ImVec2(x1, lane.bottom), color32(lc.edge));
        }
      }
    };
    for (const wv::LanePlacement placement : kPlacements) {
      const std::vector<const ShownLane*>& regionLanes = regionChannelLanes[static_cast<std::size_t>(placement)];
      if (regionLanes.empty()) {
        continue;
      }
      std::vector<WaveformRenderer::Lane> pass;
      for (const ShownLane* shown : regionLanes) {
        const WaveformRenderer::Lane lane = passLane(shown->lane.source, shown->top);
        pass.push_back(lane);
        lanes.emplace_back(lane, shown);
      }
      if (renderer != nullptr) {
        draw->PushClipRect(ImVec2(traceMin.x, regionLanes.front()->clipTop),
                           ImVec2(traceMax.x, regionLanes.front()->clipBottom), true);
        renderer->draw(draw, regionView(waveform->view, placement), traceLeft, pass, lineWidth);
        draw->PopClipRect();
      }
    }
    if (renderer == nullptr) {
      draw->AddText(ImVec2(traceLeft + em, visibleTop + em), mutedColor,
                    tr("Traces cannot be drawn: this platform has no waveform pass yet."));
    }
    if (total == 0) {
      const bool problem = !live && !waveform->note.empty();
      const std::string text = live ? std::string(tr("Waiting for samples..."))
                               : problem ? waveform->note
                                         : std::string(tr("No capture yet: press Start"));
      const float wrap = (std::max)(em * 8.0f, traceWidth - em * 2.0f);
      const ImVec2 size = ImGui::CalcTextSize(text.c_str(), nullptr, false, wrap);
      const ImVec2 at(traceLeft + (std::max)(em, (traceWidth - size.x) * 0.5f),
                      scrollAreaTop + (std::max)(em, ((scrollAreaBottom - scrollAreaTop) - size.y) * 0.5f));
      draw->AddText(nullptr, 0.0f, at, problem ? ImGui::GetColorU32(errorTextColor(dark)) : mutedColor,
                    text.c_str(), nullptr, wrap);
    }
    const float baseDotRadius = (std::max)(1.5f, 2.0f * scale);
    const float physicalPixel = 1.0f / framebufferScale;
    const float lineHalfWidth = lineWidth * 0.5f;
    const wv::Viewport dotColumns = columns();
    for (const auto& [lane, laneShown] : lanes) {
      draw->PushClipRect(ImVec2(traceMin.x, laneShown->clipTop), ImVec2(traceMax.x, laneShown->clipBottom), true);
      paintDenseSpans(lane);
      const wv::LaneColors lc =
        wv::laneColors(toWaveformViewStyle(signalStyle_), laneChoice(lane.row), dark);
      const ImU32 laneColor = color32(lc.dot);
      const ImU32 ringColor = color32(lc.outlineColor);
      const wv::DotGeometry geometry =
        wv::dotGeometry(lc.outline, baseDotRadius, lineHalfWidth, physicalPixel);
      for (const double x : waveform->columns.dots()[lane.row]) {
        const double at = std::floor(dotColumns.pixelToSample(x));
        if (at < 0.0 || at >= static_cast<double>(total)) {
          continue;
        }
        const bool high = source.level(lane.row, static_cast<std::uint64_t>(at));
        const wv::PixelLevel level = high ? wv::PixelLevel::High : wv::PixelLevel::Low;
        const float y =
          wv::overlayLineY(level, lc.outline, lane.top, lane.bottom, lineWidth, framebufferScale);
        const ImVec2 center(static_cast<float>(wv::overlayColumnX(traceLeft, framebufferScale, x)), y);
        if (geometry.ring) {
          draw->AddCircleFilled(center, geometry.outerRadius, ringColor);
          draw->AddCircleFilled(center, geometry.innerRadius, laneColor);
        } else {
          draw->AddCircleFilled(center, geometry.outerRadius, laneColor);
        }
      }
      draw->PopClipRect();
    }

    if (waveform->showCursors && waveform->cursors) {
      const float x1 = columnX(waveform->cursors->first());
      const float x2 = columnX(waveform->cursors->second());
      fillPixels(draw, ImVec2((std::min)(x1, x2), visibleTop), ImVec2((std::max)(x1, x2), visibleBottom),
                 withAlpha(accent, 0.10f));
      draw->PushClipRect(ImVec2(traceLeft, origin.y), ImVec2(traceLeft + traceWidth, visibleBottom),
                        kIntersectWithCurrentClipRect);
      verticalLine(x1, flagBottomY + 0.5f, visibleBottom, accent, lineWidth);
      verticalLine(x2, flagBottomY + 0.5f, visibleBottom, accent, lineWidth);
      draw->PopClipRect();
      if (const auto geometry = widthLabelGeometry(*waveform->cursors)) {
        const auto& [label, rect] = *geometry;
        draw->AddRectFilled(ImVec2(static_cast<float>(rect.minX), static_cast<float>(rect.minY)),
                            ImVec2(static_cast<float>(rect.maxX), static_cast<float>(rect.maxY)), accent,
                            em * 0.25f);
        draw->AddText(ImVec2(static_cast<float>(rect.minX + em * 0.3), static_cast<float>(rect.minY + em * 0.1)),
                      IM_COL32(255, 255, 255, 255), label.c_str());
      }
    }

    if (!waveform->markers.empty()) {
      const ImU32 markerLineColor = color32(wv::markerColor(dark));
      draw->PushClipRect(ImVec2(traceLeft, origin.y), ImVec2(traceLeft + traceWidth, visibleBottom),
                        kIntersectWithCurrentClipRect);
      for (const wv::Marker& marker : waveform->markers.markers()) {
        const float x = columnX(marker.sample);
        if (x < traceMin.x - 1.0f || x > traceMax.x + 1.0f) {
          continue;
        }
        verticalLine(x, flagBottomY + 0.5f, visibleBottom, markerLineColor, lineWidth);
      }
      draw->PopClipRect();
    }

    if (showPointerLine_ && tracesHovered && !laneDragging && ImGui::IsMousePosValid()) {
      const float dotLength = 2.0f * lineWidth;
      const float pointerX = std::floor(io.MousePos.x * framebufferScale) / framebufferScale;
      const ImU32 pointerColor = withAlpha(textColor, 0.6f);
      draw->PushClipRect(ImVec2(traceLeft, origin.y), ImVec2(traceLeft + traceWidth, visibleBottom),
                        kIntersectWithCurrentClipRect);
      for (float y = flagBottomY; y < visibleBottom; y += 2.0f * dotLength) {
        fillPixels(draw, ImVec2(pointerX, y), ImVec2(pointerX + lineWidth, (std::min)(y + dotLength, visibleBottom)),
                   pointerColor);
      }
      draw->PopClipRect();
    }

    if (waveform->trimPoint && *waveform->trimPoint <= total) {
      const float x = columnX(*waveform->trimPoint);
      draw->PushClipRect(ImVec2(traceLeft, origin.y), ImVec2(traceLeft + traceWidth, visibleBottom),
                        kIntersectWithCurrentClipRect);
      const ImU32 deleteColor = ImGui::ColorConvertFloat4ToU32(errorTextColor(dark));
      if (waveform->trimHovered) {
        const bool before = *waveform->trimHovered == gc::TrimSide::Before;
        fillPixels(draw, ImVec2(before ? traceLeft : x, visibleTop),
                   ImVec2(before ? x : traceLeft + traceWidth, visibleBottom), withAlpha(deleteColor, 0.18f));
      }
      verticalLine(x, flagBottomY + 0.5f, visibleBottom, deleteColor, 2.0f * lineWidth);
      draw->PopClipRect();
    }

    const ShownLane* pulseLane = nullptr;
    if (tracesHovered && !tracesActive && !laneDragging && ImGui::IsMousePosValid()) {
      for (const ShownLane& shown : shownLanes) {
        if (shown.lane.key.kind == wv::LaneKind::Channel && io.MousePos.y >= shown.visibleTop() &&
            io.MousePos.y < shown.visibleBottom() &&
            wv::waveHitAtY(io.MousePos.y, shown.top, pitch, waveOffsets.top, waveOffsets.bottom, 1)) {
          pulseLane = &shown;
          break;
        }
      }
    }
    if (pulseLane != nullptr) {
      const LaneClip overlayClip(ImVec2(traceMin.x, pulseLane->clipTop), ImVec2(traceMax.x, pulseLane->clipBottom));
      const auto sample = wv::sampleUnderPixel(viewport, mouseX, total);
      if (sample) {
        const std::uint32_t channel = pulseLane->lane.source;
        if (const auto pulse = wv::measurePulse(source, columns(), channel, *sample)) {
          const float laneTop = pulseLane->top;
          const float waveTop = std::round(laneTop + waveOffsets.top);
          const float waveBottom = std::round(laneTop + waveOffsets.bottom);
          const float start = static_cast<float>(wv::overlayColumnX(traceLeft, framebufferScale, pulse->startPixelX));
          const float mid = static_cast<float>(wv::overlayColumnX(traceLeft, framebufferScale, pulse->midPixelX));
          const float end = static_cast<float>(wv::overlayColumnX(traceLeft, framebufferScale, pulse->endPixelX));
          const wv::DimensionRows rows =
            wv::dimensionRows(waveTop, waveBottom, labelFrameHeight, dimensionRowGap);
          const float highY = static_cast<float>(rows.highY);
          const float periodY = static_cast<float>(rows.periodY);
          const std::string halfLabel = gui_format::formatTimePs(pulse->pointerHalfPicoseconds);
          const std::string periodLabel = gui_format::formatTimePs(pulse->periodPicoseconds);
          const std::array<std::pair<float, wv::PulseEdge>, 3> edges{
            {{start, wv::PulseEdge::Start}, {mid, wv::PulseEdge::Mid}, {end, wv::PulseEdge::End}}};
          for (const auto& [x, edge] : edges) {
            const wv::DimensionRowReach reach = wv::dimensionRowReach(edge);
            const float top = reach.high ? highY - em * 0.35f : waveTop;
            const float bottom = reach.period ? periodY + em * 0.35f : waveBottom;
            draw->AddLine(ImVec2(x, top + 0.5f), ImVec2(x, bottom + 0.5f),
                          withAlpha(textColor, 0.35f), 1.0f);
          }
          dimensionLine(draw, start, mid, highY, textColor, halfLabel, lineWidth, traceMin.x, traceMax.x);
          dimensionLine(draw, start, end, periodY, textColor, periodLabel, lineWidth, traceMin.x, traceMax.x);
        }
      }
    }
    draw->PopClipRect();

    const ImU32 stackRuleColor = withAlpha(mutedColor, 0.5f);
    if (regions.topHeight > 0.0) {
      const float y = visibleTop + static_cast<float>(regions.topTop + regions.topHeight);
      draw->AddLine(pixelCentre(origin.x, y), pixelCentre(traceLeft + traceWidth, y), stackRuleColor, 1.0f);
    }
    if (regions.bottomHeight > 0.0) {
      const float y = visibleTop + static_cast<float>(regions.bottomTop);
      draw->AddLine(pixelCentre(origin.x, y), pixelCentre(traceLeft + traceWidth, y), stackRuleColor, 1.0f);
    }

    const double pointerY = static_cast<double>(io.MousePos.y - visibleTop);
    const std::optional<wv::DraggedLanePlace> draggedPlace =
      laneDragging ? wv::draggedLanePlace(allLanes, *waveform->laneDrag.dragged, pointerY,
                                          static_cast<double>(waveform->laneDrag.grabOffset),
                                          static_cast<double>(lanesHeight))
                   : std::nullopt;
    if (gutterWidth > 0.0f) {
      const wv::BracketGutter gutter{static_cast<double>(origin.x - gutterWidth), static_cast<double>(origin.x),
                                     static_cast<double>(bracketStep)};
      const wv::BracketRect gutterClip{gutter.left, static_cast<double>(visibleTop), gutter.right,
                                       static_cast<double>(visibleBottom)};
      const float channelAnchor = (pitch - headerContentHeight) * 0.5f + em * 0.5f;
      const auto placed = [&](wv::LaneKey key, float anchor) {
        if (draggedPlace && key == *waveform->laneDrag.dragged) {
          return wv::draggedBracketLane(static_cast<double>(visibleTop) + draggedPlace->top, draggedPlace->height,
                                        static_cast<double>(anchor), gutterClip);
        }
        for (const ShownLane& lane : placedLanes) {
          if (lane.lane.key == key) {
            return wv::BracketLane{true, static_cast<double>(lane.top), lane.lane.height, static_cast<double>(anchor),
                                   static_cast<double>(lane.clipTop), static_cast<double>(lane.clipBottom)};
          }
        }
        return wv::BracketLane{false};
      };
      const float decoderAnchor = decoderPitch * 0.5f;
      const float thickness = 2.0f * lineWidth;
      const auto snap = [&](float v) { return std::round(v * framebufferScale) / framebufferScale; };
      draw->PushClipRect(ImVec2(static_cast<float>(gutter.left), visibleTop), ImVec2(origin.x, visibleBottom), false);
      for (std::size_t d = 0; d < decoderCount; ++d) {
        if (!bracketSlotOf[d]) continue;
        std::vector<wv::BracketLane> reads;
        for (const std::uint32_t device : decoderReads[d].channels) {
          reads.push_back(placed(wv::LaneKey{wv::LaneKind::Channel, device}, channelAnchor));
        }
        for (const std::size_t lower : decoderReads[d].decoders) {
          reads.push_back(placed(wv::LaneKey{wv::LaneKind::Decoder, static_cast<std::uint32_t>(lower)}, decoderAnchor));
        }
        const wv::BracketLane own = placed(wv::LaneKey{wv::LaneKind::Decoder, static_cast<std::uint32_t>(d)}, decoderAnchor);
        const std::optional<wv::ColorChoice>& choice = ui.decoders[d].color();
        const ImU32 ink = choice ? color32(wv::outlineColorFor(*choice, dark)) : textColor;
        for (const wv::BracketSegment& segment : wv::bracketSegments(own, reads, *bracketSlotOf[d], gutter, gutterClip)) {
          const float half = thickness * 0.5f;
          const bool vertical = segment.kind == wv::BracketSegmentKind::Span;
          const float left = snap(static_cast<float>(segment.x0) - half);
          const float top = snap(static_cast<float>(segment.y0) - half);
          const ImVec2 min(left, top);
          const ImVec2 max(vertical ? left + thickness : static_cast<float>(segment.x1),
                           vertical ? snap(static_cast<float>(segment.y1) + half) : top + thickness);
          fillPixels(draw, min, max, ink);
        }
      }
      draw->PopClipRect();
    }

    if (waveform->laneDrag.dragged) {
      ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
      const std::vector<double> topHeights = wv::dropHeights(laneDisplay, wv::LanePlacement::Top, lanePad);
      const std::vector<double> scrollHeights = wv::dropHeights(laneDisplay, wv::LanePlacement::Scroll, lanePad);
      const std::vector<double> bottomHeights = wv::dropHeights(laneDisplay, wv::LanePlacement::Bottom, lanePad);
      const wv::DropTarget target =
        wv::dropTarget(pointerY, regions, topHeights, scrollHeights, bottomHeights, static_cast<double>(laneScroll));
      const wv::LaneDragStep dragStep =
        wv::laneDragStep(ImGui::IsMouseDown(ImGuiMouseButton_Left), ImGui::IsKeyPressed(ImGuiKey_Escape, false));
      if (dragStep == wv::LaneDragStep::Cancel) {
        if (renderer != nullptr) {
          renderer->release(ghostView(waveform->view));
        }
        waveform->laneDrag.dragged.reset();
        waveform->laneDrag.candidate.reset();
      } else if (dragStep == wv::LaneDragStep::Drop) {
        if (ImGui::IsMousePosValid()) {
          applyLaneAction(ui, wv::dropLane(ui.laneOrder, *waveform->laneDrag.dragged, target.placement, target.index,
                                           laneDisplay.lanesOf(target.placement)));
        }
        if (renderer != nullptr) {
          renderer->release(ghostView(waveform->view));
        }
        waveform->laneDrag.dragged.reset();
      } else {
        draw->PushClipRect(ImVec2(origin.x, visibleTop), ImVec2(traceLeft + traceWidth, visibleBottom), false);
        const auto dropZone = [&](wv::LanePlacement placement, double top, double height, const char* label) {
          if (height <= 0.0 || !laneDisplay.lanesOf(placement).empty()) {
            return;
          }
          const ImVec2 min(origin.x, visibleTop + static_cast<float>(top));
          const ImVec2 max(traceLeft + traceWidth, visibleTop + static_cast<float>(top + height));
          fillPixels(draw, min, max, withAlpha(accent, 0.08f));
          const ImVec2 size = ImGui::CalcTextSize(label);
          draw->AddText(ImVec2(origin.x + style.ItemSpacing.x, min.y + ((max.y - min.y) - size.y) * 0.5f), mutedColor,
                        label);
        };
        dropZone(wv::LanePlacement::Top, regions.topTop, regions.topHeight, tr("Pin to top"));
        dropZone(wv::LanePlacement::Bottom, regions.bottomTop, regions.bottomHeight, tr("Pin to bottom"));
        const wv::DisplayedLane* draggedLane = nullptr;
        for (const wv::LanePlacement placement : kPlacements) {
          for (const wv::DisplayedLane& lane : allLanes.lanesOf(placement)) {
            if (lane.key == *waveform->laneDrag.dragged) {
              draggedLane = &lane;
            }
          }
        }
        if (draggedLane != nullptr && draggedPlace) {
          const float ghostTop = visibleTop + static_cast<float>(draggedPlace->top);
          const float ghostBottom = ghostTop + static_cast<float>(draggedPlace->height);
          const float ghostRight = traceLeft + traceWidth;
          const float edge = (std::max)(1.0f, std::round(scale));
          fillPixels(draw, ImVec2(origin.x + edge * 2.0f, ghostTop + edge * 2.0f),
                              ImVec2(ghostRight + edge * 2.0f, ghostBottom + edge * 2.0f), IM_COL32(0, 0, 0, 40));
          fillPixels(draw, ImVec2(origin.x, ghostTop), ImVec2(traceLeft, ghostBottom),
                              ImGui::GetColorU32(ImGuiCol_WindowBg));
          fillPixels(draw, ImVec2(traceLeft, ghostTop), ImVec2(ghostRight, ghostBottom), background);
          if (draggedLane->key.kind == wv::LaneKind::Channel) {
            const std::uint32_t c = draggedLane->source;
            fillPixels(draw, ImVec2(traceLeft, ghostTop), ImVec2(ghostRight, ghostBottom),
                                color32(wv::laneBackgroundFor(laneChoice(c), dark)));
            const float headerBlockTop = ghostTop + (pitch - headerContentHeight) * 0.5f;
            const float x = origin.x + style.ItemSpacing.x;
            const float chipTop = headerBlockTop + (em - chipHeight) * 0.5f;
            drawChannelChip(ImVec2(x, chipTop), c);
            drawChannelName(ImVec2(x + chipWidth + nameGap, chipTop), c);
            if (renderer != nullptr) {
              const std::uint64_t ghostPass = ghostView(waveform->view);
              if (waveform->passUploads.needed(ghostPass, renderer->holds(ghostPass, columnCount, channels))) {
                BITOSCOPE_DRAW_PART(ghostUploadPart, frameProbe_, Upload);
                renderer->upload(ghostPass, columnCount, channels, waveform->columns.levels());
                waveform->passUploads.uploaded(ghostPass);
              }
              const std::array<WaveformRenderer::Lane, 1> pass{passLane(c, ghostTop)};
              draw->PushClipRect(ImVec2(traceMin.x, ghostTop), ImVec2(traceMax.x, ghostBottom), true);
              renderer->draw(draw, ghostPass, traceLeft, pass, lineWidth);
              paintDenseSpans(pass.front());
              draw->PopClipRect();
            }
          } else if (drawDecoderLane) {
            drawDecoderLane(ShownLane{*draggedLane, wv::LanePlacement::Scroll, ghostTop, visibleTop, visibleBottom}, true);
          }
          addEdgeCentredOutline(draw, ImVec2(origin.x, ghostTop), ImVec2(ghostRight, ghostBottom), accent, 0.0f, edge * 1.5f);
        }
        const float thickness = (std::max)(2.0f, std::round(2.0f * scale));
        const float lineY = std::clamp(visibleTop + static_cast<float>(target.insertionLineY), visibleTop + thickness * 0.5f,
                                       visibleBottom - thickness * 0.5f);
        draw->AddLine(pixelCentre(origin.x, lineY), pixelCentre(traceLeft + traceWidth, lineY), accent, thickness);
        draw->PopClipRect();
        if (regions.maxScroll > 0.0) {
          const double step = wv::autoScrollDelta(pointerY, regions, static_cast<double>(pitch),
                                                  static_cast<double>(std::round(em * 0.5f)));
          if (step != 0.0) {
            ImGui::SetScrollY(static_cast<float>(
              std::clamp(static_cast<double>(laneScroll) + step, 0.0, regions.maxScroll)));
          }
        }
      }
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
      waveform->laneDrag.candidate.reset();
    }

    if (!pinMenuOpened && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && ImGui::IsWindowHovered() &&
        ImGui::IsMousePosValid() && io.MousePos.x >= origin.x && io.MousePos.x < traceLeft - kLaneDividerWidth * scale &&
        (laneNameHovered || !ImGui::IsAnyItemHovered())) {
      for (const ShownLane& shown : shownLanes) {
        if (io.MousePos.y >= shown.visibleTop() && io.MousePos.y < shown.visibleBottom()) {
          waveform->headerMenuLane = shown.lane.key;
          waveform->headerMenuChannel =
            shown.lane.key.kind == wv::LaneKind::Channel ? laneDeviceOf(shown.lane.source) : 0;
          waveform->rowFormatTarget.reset();
          if (shown.lane.key.kind == wv::LaneKind::Decoder) {
            const gd::DecoderLaneModel& lane = decoderLanes[shown.lane.source];
            const gd::DecoderConfig& config = ui.decoders[shown.lane.source];
            const auto hit = wv::decoderRowAtY(io.MousePos.y, shown.top, decoderPitch, lane.rows.size());
            if (hit && lane.takesFormat[*hit]) {
              waveform->rowFormatTarget = std::pair{config.id(), lane.rows[*hit].id};
            }
          }
          gui_popup::open("lane_header_menu", popupOwner);
          break;
        }
      }
    }
  }
  ImGui::EndChild();
  drawFindBox(*waveform, traceLeft + traceWidth, origin.y + rulerHeight, dark);

  ImDrawList* draw = ImGui::GetWindowDrawList();
  const ImVec2 rulerMin(traceLeft, origin.y);
  const ImVec2 rulerMax(traceLeft + traceWidth, origin.y + rulerHeight);
  fillPixels(draw, ImVec2(origin.x - gutterWidth, origin.y), rulerMax, ImGui::GetColorU32(ImGuiCol_MenuBarBg));
  draw->PushClipRect(rulerMin, rulerMax, true);
  for (const wv::RulerTick& tick : ticks) {
    const float x = columnX(static_cast<std::uint64_t>(tick.sample));
    const float length = rulerHeight * (tick.major ? 0.4f : 0.2f);
    draw->AddLine(ImVec2(x, rulerMax.y - length + 0.5f), ImVec2(x, rulerMax.y + 0.5f), tick.major ? textColor : mutedColor);
    if (tick.major && !tick.label.empty()) {
      const ImVec2 size = ImGui::CalcTextSize(tick.label.c_str());
      const float textX = traceLeft + static_cast<float>(tick.labelPixelX);
      draw->AddText(ImVec2(textX, origin.y + (rulerHeight * 0.6f - size.y) * 0.5f), textColor, tick.label.c_str());
    }
  }
  if (waveform->showCursors && waveform->cursors) {
    const wv::CursorPair& pair = *waveform->cursors;
    const float x1 = columnX(pair.first());
    const float x2 = columnX(pair.second());
    drawFlag(draw, x1, flagTop, accent, std::string(), em);
    drawFlag(draw, x2, flagTop, accent, std::string(), em);
  }
  if (!waveform->markers.empty()) {
    const ImU32 markerLineColor = color32(wv::markerColor(dark));
    const std::vector<wv::Marker>& markerList = waveform->markers.markers();
    const std::vector<std::string> labels =
      wv::markerLabels(waveform->markers, waveform->selectedMarker, hoveredFlag, source.sampleRateMilliHz());
    for (std::size_t i = 0; i < markerList.size(); ++i) {
      const wv::Marker& marker = markerList[i];
      const float x = columnX(marker.sample);
      if (x < rulerMin.x - 1.0f || x > rulerMax.x + 1.0f) {
        continue;
      }
      drawFlag(draw, x, flagTop, markerLineColor, labels[i], em);
      if (waveform->selectedMarker == marker.id) {
        const ImRect flagRect = markerFlagRect(x, flagTop, labels[i], em);
        draw->AddRect(flagRect.Min, flagRect.Max, selectionColor, em * 0.15f, selectionFrameWidth,
                      ImDrawFlags_StrokeOutside);
      }
    }
  }
  draw->PopClipRect();
  const std::string rate = gui_format::formatFrequency(source.sampleRateMilliHz());
  draw->AddText(ImVec2(origin.x + style.ItemSpacing.x, origin.y + (rulerHeight - em) * 0.5f), mutedColor,
                rate.c_str());
  if (ImGui::IsMouseHoveringRect(origin, ImVec2(traceLeft, origin.y + rulerHeight)) && ImGui::IsWindowHovered()) {
    ImGui::SetTooltip("%s", trf("{} at {}, {} channels",
                                trfn("{} sample", "{} samples",
                                     static_cast<unsigned long>((std::min)(total, std::uint64_t{0xFFFFFFFF})),
                                     gui_format::withThousands(total)),
                                rate, channels)
                              .c_str());
  }

  scrollbarPaint(scrollBar, scrollBarId, wv::scrollRangeFor(viewport, viewTotal));

  const bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
  if (focused && !io.WantTextInput && waveform->selectedMarker && ImGui::IsKeyPressed(ImGuiKey_Delete)) {
    waveform->markers.remove(*waveform->selectedMarker);
    waveform->selectedMarker.reset();
    ui.capture.markViewEdited();
  }
  if (focused && !io.WantTextInput && waveform->selectedMarker &&
      !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel) &&
      ImGui::IsKeyPressed(ImGuiKey_Escape)) {
    waveform->selectedMarker.reset();
  }
  BITOSCOPE_DRAW_PART_NEXT(waveformPart, Popups);
  if (gui_popup::begin("lane_pin_menu", popupOwner)) {
    const std::optional<wv::LanePlacement> placement =
      waveform->laneMenuKey ? ui.laneOrder.placementOfKey(*waveform->laneMenuKey) : std::nullopt;
    if (placement) {
      const wv::LaneKey key = *waveform->laneMenuKey;
      if (ImGui::MenuItem(tr("Pin to top"), nullptr, false, *placement != wv::LanePlacement::Top)) {
        applyLaneAction(ui, wv::pinLaneTo(ui.laneOrder, key, wv::PinSide::Top));
      }
      if (ImGui::MenuItem(tr("Pin to bottom"), nullptr, false, *placement != wv::LanePlacement::Bottom)) {
        applyLaneAction(ui, wv::pinLaneTo(ui.laneOrder, key, wv::PinSide::Bottom));
      }
      if (ImGui::MenuItem(tr("Unpin"), nullptr, false, *placement != wv::LanePlacement::Scroll)) {
        applyLaneAction(ui, wv::unpinLane(ui.laneOrder, key));
      }
    } else {
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
  const auto rowFormatItems = [&] {
    if (!waveform->rowFormatTarget) return false;
    const auto& [decoderId, row] = *waveform->rowFormatTarget;
    const auto config = std::ranges::find(ui.decoders, decoderId, &gd::DecoderConfig::id);
    if (config == ui.decoders.end()) return false;
    const decode::DataFormat current = config->rowDataFormat(row);
    for (const decode::DataFormat choice : kDataFormats) {
      if (ImGui::MenuItem(dataFormatLabel(choice), nullptr, current == choice) && config->setRowDataFormat(row, choice)) {
        ui.capture.markDecodersEdited();
      }
    }
    return true;
  };
  if (gui_popup::begin("lane_header_menu", popupOwner)) {
    if (waveform->headerMenuLane && waveform->headerMenuLane->kind == wv::LaneKind::Channel &&
        ImGui::MenuItem(tr("Rename channel"))) {
      gui_lane_header::beginNameEdit(
        waveform->nameEdit, *waveform->headerMenuLane,
        gc::shownChannelName(ui.channelUserNames, ui.channelNames, waveform->headerMenuChannel));
    }
    if (rowFormatItems()) {
      ImGui::Separator();
    }
    if (ImGui::MenuItem(tr("Reset lane order"))) {
      resetLaneOrder(ui);
    }
    ImGui::EndPopup();
  }
  bool trimMenuShown = false;
  if (gui_popup::begin("row_format_menu", popupOwner)) {
    const bool formats = rowFormatItems();
    if (waveform->trimPoint) {
      trimMenuShown = true;
      if (formats) {
        ImGui::Separator();
      }
      trimMenuItems(sessionId, ui);
    } else if (!formats) {
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
  if (gui_popup::begin("trace_menu", popupOwner)) {
    trimMenuShown = true;
    trimMenuItems(sessionId, ui);
    ImGui::EndPopup();
  }
  if (!trimMenuShown) {
    waveform->trimPoint.reset();
    waveform->trimHovered.reset();
  }
  if (gui_popup::begin("marker_context", popupOwner)) {
    if (waveform->contextMarker) {
      const wv::MarkerId id = *waveform->contextMarker;
      if (ImGui::MenuItem(tr("Delete"))) {
        waveform->markers.remove(id);
        waveform->contextMarker.reset();
        ui.capture.markViewEdited();
        if (waveform->selectedMarker == id) {
          waveform->selectedMarker.reset();
        }
      }
      if (ImGui::MenuItem(tr("Delete all")) && waveform->markers.removeAll()) {
        waveform->contextMarker.reset();
        waveform->selectedMarker.reset();
        ui.capture.markViewEdited();
      }
      if (ImGui::MenuItem(tr("Rename"))) {
        openMarkerRename(*waveform, id, popupOwner);
      }
    }
    ImGui::EndPopup();
  }
  if (gui_popup::begin("marker_rename", popupOwner)) {
    ImGui::TextUnformatted(tr("Marker name"));
    if (waveform->renameFocusPending) {
      ImGui::SetKeyboardFocusHere();
      waveform->renameFocusPending = false;
    }
    const bool submitted =
      gui_text_field::committedInputText("##marker_name", waveform->renameBuffer.data(), waveform->renameBuffer.size(),
                                         ImGuiInputTextFlags_AutoSelectAll) != gui_text_field::Commit::None;
    ImGui::SameLine();
    const bool clickedOk = ImGui::Button(tr("Rename"));
    if ((submitted || clickedOk) && waveform->renamingMarker &&
        waveform->markers.rename(*waveform->renamingMarker, waveform->renameBuffer.data())) {
      ui.capture.markViewEdited();
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
  const auto colorChoiceBody = [&](const std::function<void(wv::ColorChoice)>& pick, bool canReset,
                                   const char* resetId, const char* resetTooltip) {
    return gui::colorChoiceBody(waveform->colorDraft, pick, canReset, resetId, resetTooltip, dark);
  };
  if (gui_popup::begin("channel_color_picker", popupOwner)) {
    if (waveform->colorPickerChannel && *waveform->colorPickerChannel < ui.channelColors.size()) {
      const std::uint32_t channel = *waveform->colorPickerChannel;
      ImGui::TextUnformatted(tr("Channel color"));
      const bool clickedReset = colorChoiceBody(
        [&](wv::ColorChoice choice) {
          ui.channelColors[channel] = choice;
          ui.capture.markViewEdited();
        },
        gc::canResetChannelColor(ui.channelColors, ui.channelDefaultColors, channel), "channel_color/reset",
        tr("Restore default color of channel"));
      if (clickedReset && gc::resetChannelColor(ui.channelColors, ui.channelDefaultColors, channel)) {
        ui.capture.markViewEdited();
        ImGui::CloseCurrentPopup();
      }
    }
    ImGui::EndPopup();
  }
  if (gui_popup::begin("decoder_color_picker", popupOwner)) {
    const auto config = std::ranges::find_if(ui.decoders, [&](const gd::DecoderConfig& candidate) {
      return waveform->colorPickerDecoder && candidate.id() == *waveform->colorPickerDecoder;
    });
    if (config != ui.decoders.end()) {
      DecoderSettingsChange change;
      ImGui::TextUnformatted(tr("Decoder color"));
      const bool clickedReset = colorChoiceBody(
        [&](wv::ColorChoice choice) {
          if (config->color() != choice) {
            config->setColor(choice);
            change.presentation = true;
          }
        },
        config->color().has_value(), "decoder_color/reset", tr("Restore default color of decoder"));
      if (clickedReset && config->color()) {
        config->setColor(std::nullopt);
        change.presentation = true;
        ImGui::CloseCurrentPopup();
      }
      applyDecoderSettingsChange(sessionId, ui, change);
    } else {
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
  ImGui::EndChild();
}

}
