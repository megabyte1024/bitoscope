/*
 * SPDX-License-Identifier: MIT
 */

#include "find_box.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <display_format.h>
#include <find_lanes.h>
#include <find_marks.h>
#include <find_matches.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "gui_app.h"
#include "session_waveform.h"
#include "theme.h"
#include "tr.h"

namespace bitoscope::gui {

namespace {

namespace gd = gui_decode;
namespace wv = waveform_view;

constexpr float kFieldEm = 12.0f;
constexpr float kResultEm = 7.0f;
constexpr float kMarginEm = 0.3f;
constexpr float kPaddingEm = 0.3f;
constexpr float kCloseCrossFraction = 0.2f;
constexpr float kMarkFillAlpha = 0.35f;
constexpr std::size_t kMarkVisitsPerLane = 200'000;

struct ResultText {
  std::string text;
  bool problem = false;
};

const char* patternErrorText(gd::FindPatternError error) {
  switch (error) {
  case gd::FindPatternError::OddHexDigits:
    return tr("Odd number of hex digits");
  case gd::FindPatternError::NotHex:
    return tr("Hex digits and spaces only");
  case gd::FindPatternError::NotAscii:
    return tr("Printable ASCII only");
  case gd::FindPatternError::Empty:
    break;
  }
  return "";
}

ResultText resultText(const FindBox& find) {
  if (find.notPrintable) {
    return {tr("Printable ASCII only"), true};
  }
  if (!find.search) {
    return {};
  }
  const gd::FindSearch& search = *find.search;
  switch (search.state()) {
  case gd::FindState::NoText:
    return {};
  case gd::FindState::PatternError:
    return {search.patternError() ? patternErrorText(*search.patternError()) : "", true};
  case gd::FindState::Searching:
    return {tr("Searching..."), false};
  case gd::FindState::OutOfMemory:
    return {tr("Out of memory"), true};
  case gd::FindState::Failed:
    return {tr("Search failed"), true};
  case gd::FindState::Ready:
    break;
  }
  const std::size_t count = search.matches().size();
  if (count == 0) {
    return {tr("No results"), false};
  }
  const std::size_t shown = search.current() ? *search.current() + 1 : 0;
  return {trf("{} of {}", gui_format::withThousands(shown), gui_format::withThousands(count)), false};
}

int findCharFilter(ImGuiInputTextCallbackData* data) {
  const gd::FindMode mode = *static_cast<const gd::FindMode*>(data->UserData);
  return gd::acceptsFindChar(mode, static_cast<unsigned int>(data->EventChar)) ? 0 : 1;
}

void tooltipFor(const char* text) {
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled)) {
    ImGui::SetTooltip("%s", text);
  }
}

bool toggleButton(const std::string& label, bool on, const char* tip) {
  if (on) {
    ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_Header));
  }
  const bool pressed = ImGui::Button(label.c_str());
  if (on) {
    ImGui::PopStyleColor();
  }
  tooltipFor(tip);
  return pressed;
}

float buttonWidth(const std::string& label) {
  constexpr bool hideTextAfterDoubleHash = true;
  return ImGui::CalcTextSize(label.c_str(), nullptr, hideTextAfterDoubleHash).x +
         ImGui::GetStyle().FramePadding.x * 2.0f;
}

void setText(FindBox& find, std::string_view text) {
  const std::size_t n = (std::min)(text.size(), find.text.size() - 1);
  std::copy_n(text.begin(), n, find.text.begin());
  find.text[n] = '\0';
}

ImU32 withAlpha(ImU32 color, float alpha) {
  ImVec4 c = ImGui::ColorConvertU32ToFloat4(color);
  c.w = alpha;
  return ImGui::ColorConvertFloat4ToU32(c);
}

void closeFind(FindBox& find) {
  find.open = false;
  find.stepped = false;
  find.fieldActive = false;
  find.selectAll = false;
  find.focusFrames = 0;
}

void closeCross(ImVec2 min, ImVec2 max, float lineWidth) {
  const ImVec2 centre((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
  const float half = (max.y - min.y) * kCloseCrossFraction;
  ImDrawList* draw = ImGui::GetWindowDrawList();
  const ImU32 ink = ImGui::GetColorU32(ImGuiCol_Text);
  draw->AddLine(ImVec2(centre.x - half, centre.y - half), ImVec2(centre.x + half, centre.y + half), ink, lineWidth);
  draw->AddLine(ImVec2(centre.x - half, centre.y + half), ImVec2(centre.x + half, centre.y - half), ink, lineWidth);
}

}

void switchFindMode(FindBox& find, gd::FindMode to) {
  const gd::FindMode from = find.mode;
  if (from == to) {
    return;
  }
  const bool textIsUnconvertedHex = find.notPrintable;
  if (textIsUnconvertedHex) {
    find.notPrintable = false;
    find.mode = to;
    return;
  }
  const std::string_view text(find.text.data());
  if (const std::optional<std::string> converted = gd::convertFindText(text, from, to)) {
    if (converted->size() >= find.text.size()) {
      return;
    }
    setText(find, *converted);
  } else {
    find.notPrintable = to == gd::FindMode::Ascii && gd::parseFindPattern(text, from).has_value();
  }
  find.mode = to;
}

void drawFindMarks(ImDrawList* draw, const FindBox& find, const FindMarkLane& lane) {
  if (!find.search || find.search->state() != gd::FindState::Ready) {
    return;
  }
  const gd::FindSearch& search = *find.search;
  const std::optional<std::size_t> current = search.current();
  if (!find.open && !(find.stepped && current)) {
    return;
  }
  const gd::FindMatches& matches = search.matches();
  const auto markRect = [&](std::size_t slot, std::uint64_t begin, std::uint64_t end) {
    float left = lane.sampleX(begin);
    float right = lane.sampleX(end);
    if (right - left < lane.minWidth) {
      left = std::floor((left + right - lane.minWidth) * 0.5f);
      right = left + lane.minWidth;
    }
    const float top = lane.top + static_cast<float>(slot) * lane.rowPitch;
    return ImRect(left, top, right, top + lane.rowPitch);
  };
  const ImU32 fill = withAlpha(lane.color, kMarkFillAlpha);
  draw->PushClipRect(lane.clipMin, lane.clipMax, true);
  if (find.open) {
    const gd::FindMarkScale scale{lane.samplesPerPixel, lane.samplesPerPixel * lane.minWidth};
    for (const gd::FindMarkRun& run : gd::findMarkRuns(matches, lane.decoder, lane.rows, lane.firstSample,
                                                       lane.endSample, scale, kMarkVisitsPerLane)) {
      const ImRect r = markRect(run.slot, run.begin, run.end);
      draw->AddRectFilled(r.Min, r.Max, fill);
    }
  }
  if (current) {
    const gd::FindMatch& match = matches.at(*current);
    const std::optional<std::size_t> slot =
      match.decoder == lane.decoder && gd::FindMatches::overlaps(match, lane.firstSample, lane.endSample)
        ? gd::shownRowSlot(lane.rows, match.row)
        : std::nullopt;
    if (slot) {
      const ImRect r = markRect(*slot, match.begin, match.end);
      if (!find.open) {
        draw->AddRectFilled(r.Min, r.Max, fill);
      }
      const float inset = lane.frameWidth * 0.5f;
      draw->AddRect(ImVec2(r.Min.x + inset, r.Min.y + inset), ImVec2(r.Max.x - inset, r.Max.y - inset), lane.color,
                    0.0f, lane.frameWidth);
    }
  }
  draw->PopClipRect();
}

SessionWaveform* GuiApp::activeWaveform() {
  const std::optional<gui_session::SessionId> active = sessions_.active();
  if (!active) {
    return nullptr;
  }
  const auto ui = sessionUi_.find(*active);
  return ui == sessionUi_.end() ? nullptr : ui->second.waveform.get();
}

void GuiApp::openFind(SessionWaveform& waveform) {
  FindBox& find = waveform.find;
  find.open = true;
  find.selectAll = true;
  find.focusFrames = kFindFocusFrames;
}

void GuiApp::stepFind(SessionWaveform& waveform, bool forward) {
  FindBox& find = waveform.find;
  if (find.text[0] == '\0' || !find.search) {
    openFind(waveform);
    return;
  }
  if (!find.open) {
    find.stepped = true;
  }
  if (forward) {
    find.search->next();
  } else {
    find.search->prev();
  }
}

void GuiApp::handleFindShortcuts() {
  const ImGuiIO& io = ImGui::GetIO();
  if (ImGui::GetTopMostPopupModal() != nullptr) {
    return;
  }
  const bool findPressed = io.KeyCtrl && !io.KeyShift && !io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_F, false);
  const bool stepPressed = !io.KeyCtrl && !io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_F3, true);
  if (!findPressed && !stepPressed) {
    return;
  }
  SessionWaveform* waveform = activeWaveform();
  if (waveform == nullptr) {
    return;
  }
  if (findPressed) {
    openFind(*waveform);
  } else {
    stepFind(*waveform, !io.KeyShift);
  }
}

std::optional<gd::FindMatch> GuiApp::updateFind(SessionUi& ui, const wv::LaneDisplay& lanes,
                                                std::span<const gd::DecoderLaneModel> decoderLanes,
                                                std::uint64_t anchor) {
  if (!ui.waveform) {
    return std::nullopt;
  }
  FindBox& find = ui.waveform->find;
  if (!find.open && !find.stepped) {
    return std::nullopt;
  }
  if (!find.search) {
    find.search = std::make_unique<gd::FindSearch>(
      gd::findByteMatches, [this] { eventQueue_.post([](const gui_events::GuiEventHandlers&) {}); });
  }
  const std::vector<std::uint32_t> laneRanks = gd::findLaneRanks(lanes, decoderLanes.size());
  std::vector<std::vector<std::uint32_t>> rowRanks(decoderLanes.size());
  std::vector<gd::FindSource> sources(decoderLanes.size());
  for (std::size_t d = 0; d < decoderLanes.size(); ++d) {
    rowRanks[d] = gd::findRowRanks(decoderLanes[d].rows);
    sources[d] = gd::FindSource{static_cast<std::uint32_t>(d), ui.decoderAnnotations.drawn(d), laneRanks[d],
                                rowRanks[d]};
  }
  gd::FindInput input;
  input.text = find.notPrintable ? std::string_view() : std::string_view(find.text.data());
  input.mode = find.mode;
  input.caseSensitive = find.matchCase;
  input.sources = sources;
  input.anchor = anchor;
  input.now = std::chrono::steady_clock::now();
  find.search->update(input);
  return find.search->takeReveal();
}

void GuiApp::drawFindBox(SessionWaveform& waveform, float right, float top, bool dark) {
  FindBox& find = waveform.find;
  if (!find.open) {
    find.fieldActive = false;
    return;
  }
  const ImGuiStyle& style = ImGui::GetStyle();
  const float em = ImGui::GetFontSize();
  const float frame = ImGui::GetFrameHeight();
  const float gap = style.ItemInnerSpacing.x;
  const bool hex = find.mode == gd::FindMode::Hex;
  const std::string caseLabel = withId(tr("Aa"), "find_case");
  const std::string hexLabel = withId(tr("Hex"), "find_hex");
  const ResultText result = resultText(find);
  const float fieldWidth = std::round(em * kFieldEm);
  const float resultWidth =
    (std::max)(std::round(em * kResultEm), std::ceil(ImGui::CalcTextSize(result.text.c_str()).x));
  const ImVec2 padding(std::round(em * kPaddingEm), std::round(em * kPaddingEm));
  constexpr float kGaps = 6.0f;
  constexpr float kSquareButtons = 3.0f;
  const float inner =
    fieldWidth + buttonWidth(caseLabel) + buttonWidth(hexLabel) + resultWidth + frame * kSquareButtons + gap * kGaps;
  const float estimated = inner + padding.x * 2.0f;
  const ImVec2 size(std::ceil(find.width > 0.0f ? find.width : estimated), std::ceil(frame + padding.y * 2.0f));
  const float margin = std::round(em * kMarginEm);
  ImGui::SetCursorScreenPos(ImVec2(right - margin - size.x, top + margin));
  ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyleColorVec4(ImGuiCol_PopupBg));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, padding);
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(gap, style.ItemSpacing.y));
  const bool visible =
    ImGui::BeginChild("find_box", size, ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
  ImGui::PopStyleVar();
  const ImGuiID fieldId = ImGui::GetID("##find_text");
  // WHY: Close BEFORE the field is drawn: an active Dear ImGui field reverts its text on Escape.
  if (visible && ImGui::IsKeyPressed(ImGuiKey_Escape, false) && ImGui::IsWindowFocused()) {
    closeFind(find);
  } else if (visible) {
    ImGui::SetNextItemWidth(fieldWidth);
    if (find.focusFrames == kFindFocusFrames) {
      ImGui::SetKeyboardFocusHere();
      if (ImGui::GetActiveID() != fieldId) {
        find.selectAll = false;
      }
    }
    if (find.selectAll && ImGui::GetActiveID() == fieldId) {
      if (ImGuiInputTextState* state = ImGui::GetInputTextState(fieldId)) {
        state->ReloadUserBufAndSelectAll();
      }
    }
    find.selectAll = false;
    if (ImGui::InputTextWithHint("##find_text", tr("Find"), find.text.data(), find.text.size(),
                                 ImGuiInputTextFlags_CallbackCharFilter, findCharFilter, &find.mode)) {
      find.notPrintable = false;
    }
    const bool enter = ImGui::IsItemDeactivated() &&
                       (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false));
    find.fieldActive = ImGui::IsItemActive();
    if (find.fieldActive) {
      find.focusFrames = 0;
    } else if (find.focusFrames > 0) {
      --find.focusFrames;
    }
    if (enter) {
      stepFind(waveform, !ImGui::GetIO().KeyShift);
      find.focusFrames = kFindFocusFrames;
    }

    ImGui::SameLine();
    ImGui::BeginDisabled(hex);
    if (toggleButton(caseLabel, find.matchCase && !hex, tr("Match case"))) {
      find.matchCase = !find.matchCase;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (toggleButton(hexLabel, hex, tr("Search hex bytes"))) {
      switchFindMode(find, hex ? gd::FindMode::Ascii : gd::FindMode::Hex);
    }
    ImGui::SameLine();
    const float resultX = ImGui::GetCursorPosX();
    ImGui::AlignTextToFramePadding();
    if (result.problem) {
      ImGui::PushStyleColor(ImGuiCol_Text, errorTextColor(dark));
    }
    ImGui::TextUnformatted(result.text.c_str());
    if (result.problem) {
      ImGui::PopStyleColor();
    }
    ImGui::SameLine(resultX + resultWidth + gap);
    const bool anyMatch = find.search && !find.search->matches().empty();
    ImGui::BeginDisabled(!anyMatch);
    if (ImGui::ArrowButton("##find_prev", ImGuiDir_Up)) {
      stepFind(waveform, false);
    }
    tooltipFor(tr("Previous match (Shift+F3)"));
    ImGui::SameLine();
    if (ImGui::ArrowButton("##find_next", ImGuiDir_Down)) {
      stepFind(waveform, true);
    }
    tooltipFor(tr("Next match (F3)"));
    ImGui::EndDisabled();
    ImGui::SameLine();
    const bool closeClicked = ImGui::Button("##find_close", ImVec2(frame, frame));
    tooltipFor(tr("Close (Escape)"));
    closeCross(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), (std::max)(1.0f, std::round(uiScale())));
    find.width = ImGui::GetItemRectMax().x - ImGui::GetWindowPos().x + padding.x + style.ChildBorderSize;
    if (closeClicked) {
      closeFind(find);
    }
  }
  ImGui::EndChild();
  ImGui::PopStyleVar();
  ImGui::PopStyleColor();
}

}
