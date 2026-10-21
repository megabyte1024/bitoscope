/*
 * SPDX-License-Identifier: MIT
 */

#include "color_popup.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <string>

#include "tr.h"

namespace bitoscope::gui {

namespace wv = waveform_view;

namespace {

float channelToFloat(std::uint32_t argb, unsigned shift) {
  return static_cast<float>((argb >> shift) & 0xFFu) / 255.0f;
}

std::uint32_t floatToChannel(float value, unsigned shift) {
  const long scaled = std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f);
  return static_cast<std::uint32_t>(scaled) << shift;
}

}

void loadColorDraft(ColorDraft& draft, const std::optional<wv::ColorChoice>& current) {
  const std::uint32_t argb = current && current->kind == wv::ColorChoiceKind::Custom ? current->argb : 0xFFFFFFFFu;
  draft = {channelToFloat(argb, 16), channelToFloat(argb, 8), channelToFloat(argb, 0), channelToFloat(argb, 24)};
}

bool colorChoiceBody(ColorDraft& draft, const std::function<void(wv::ColorChoice)>& pick, bool canReset,
                     const char* resetId, const char* resetTooltip, bool dark) {
  const float em = ImGui::GetFontSize();
  ImGui::Separator();
  ImGui::TextUnformatted(tr("Palette"));
  const float swatchSize = std::round(em * 1.4f);
  const float gap = ImGui::GetStyle().ItemSpacing.x;
  constexpr std::uint32_t kPerRow = 8;
  for (std::uint32_t i = 0; i < wv::kChannelPaletteSize; ++i) {
    if (i % kPerRow != 0) {
      ImGui::SameLine(0.0f, gap);
    }
    ImGui::PushID(static_cast<int>(i));
    const wv::Rgb8 c = wv::channelColor(i, dark);
    const ImVec4 col = ImGui::ColorConvertU32ToFloat4(IM_COL32(c.r, c.g, c.b, 255));
    if (ImGui::ColorButton("##palette", col, ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoAlpha,
                           ImVec2(swatchSize, swatchSize))) {
      pick(wv::ColorChoice::palette(i));
      ImGui::CloseCurrentPopup();
    }
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("%s", std::string(wv::paletteName(i)).c_str());
    }
    ImGui::PopID();
  }
  ImGui::Separator();
  ImGui::TextUnformatted(tr("Custom color"));
  ImGui::SetNextItemWidth(em * 16.0f);
  constexpr ImGuiColorEditFlags kPickerFlags = ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_NoSidePreview |
                                               ImGuiColorEditFlags_DisplayRGB | ImGuiColorEditFlags_DisplayHSV |
                                               ImGuiColorEditFlags_DisplayHex | ImGuiColorEditFlags_InputRGB;
  const auto draftArgb = [&draft] {
    return floatToChannel(draft[3], 24) | floatToChannel(draft[0], 16) | floatToChannel(draft[1], 8) |
           floatToChannel(draft[2], 0);
  };
  const bool changed = ImGui::ColorPicker4("##custom", draft.data(), kPickerFlags);
  const float pickerHeight = ImGui::GetItemRectSize().y;
  // WHY: Own preview, since the picker's built-in one is not clickable: a click applies and closes.
  ImGui::SameLine();
  const bool clickedPreview =
    ImGui::ColorButton("##apply", ImVec4(draft[0], draft[1], draft[2], draft[3]), ImGuiColorEditFlags_NoTooltip,
                       ImVec2(std::round(em * 2.5f), pickerHeight));
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("%s", tr("Apply color"));
  }
  if (changed || clickedPreview) {
    pick(wv::ColorChoice::custom(draftArgb()));
  }
  if (clickedPreview) {
    ImGui::CloseCurrentPopup();
  }
  ImGui::Separator();
  ImGui::BeginDisabled(!canReset);
  const bool clickedReset = ImGui::Button(withId(tr("Reset to default"), resetId).c_str());
  ImGui::EndDisabled();
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
    ImGui::SetTooltip("%s", resetTooltip);
  }
  return clickedReset;
}

}
