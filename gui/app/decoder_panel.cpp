/*
 * SPDX-License-Identifier: MIT
 */

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <expected>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <channel_names.h>
#include <decode_budget.h>
#include <decoder_chain_config.h>
#include <decoder_labels.h>
#include <decoder_pairing.h>
#include <decoder_picker.h>
#include <decoder_settings.h>
#include <derivation_context.h>
#include <option_widget.h>
#include <picker_height.h>
#include <option_text_buffers.h>
#include <text_field.h>

#include "gui_app.h"
#include "platform_services.h"
#include "theme.h"
#include "tr.h"

namespace bitoscope::gui {

namespace {

namespace gd = gui_decode;

bool comboOf(const char* label, int& index, const std::vector<std::string>& items) {
  bool changed = false;
  index = std::clamp(index, 0, static_cast<int>(items.size()) - 1);
  if (items.empty()) return false;
  if (ImGui::BeginCombo(label, items[static_cast<std::size_t>(index)].c_str())) {
    for (int i = 0; i < static_cast<int>(items.size()); ++i) {
      const bool selected = i == index;
      ImGui::PushID(i);  // WHY: Two channels may share a preset name: PushID keeps their combo items distinct.
      const bool clicked = ImGui::Selectable(items[static_cast<std::size_t>(i)].c_str(), selected);
      ImGui::PopID();
      if (clicked) {
        index = i;
        changed = true;
      }
      if (selected) ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }
  return changed;
}

void showFieldError(const std::optional<std::string>& error, bool dark) {
  if (!error) return;
  ImGui::PushStyleColor(ImGuiCol_Text, errorTextColor(dark));
  ImGui::TextWrapped("%s", error->c_str());
  ImGui::PopStyleColor();
}

void boldText(const std::string& text) {
  ImGui::TextUnformatted(text.c_str());
  const ImVec2 at = ImGui::GetItemRectMin();
  const float offset = (std::max)(1.0f, std::round(ImGui::GetFontSize() / kFontSizeBase));
  ImGui::GetWindowDrawList()->AddText(ImVec2(at.x + offset, at.y), ImGui::GetColorU32(ImGuiCol_Text), text.c_str());
}

std::string fieldMessageKey(std::string_view id, std::string_view field) { return std::format("{}/{}", id, field); }

void copyToBuffer(gd::OptionTextBuffer& buffer, std::string_view text) {
  const std::size_t n = (std::min)(text.size(), buffer.size() - 1);
  std::copy_n(text.begin(), n, buffer.begin());
  buffer[n] = '\0';
}

std::string literalFormat(std::string_view text) {
  std::string out;
  for (const char c : text) {
    out += c;
    if (c == '%') out += '%';
  }
  return out;
}

log::LogLevel logLevelOf(gd::DecoderLogLine::Kind kind) {
  switch (kind) {
  case gd::DecoderLogLine::Kind::Error:
    return log::LogLevel::error;
  case gd::DecoderLogLine::Kind::Warning:
    return log::LogLevel::warning;
  case gd::DecoderLogLine::Kind::Info:
    return log::LogLevel::info;
  }
  return log::LogLevel::info;
}

}

void GuiApp::startDecoders() {
  decoders_ = std::make_unique<gd::GuiDecoders>(gd::GuiDecoders::startBesideGui(platform_.executableDirectory()));
  for (const gd::DecoderLogLine& line : decoders_->logLines()) {
    logger_.log(logLevelOf(line.kind), "decoders", line.text);
  }
}

void GuiApp::endDecoders() {
  for (auto& [id, ui] : sessionUi_) ui.decoderRunner.reset();
  decoders_.reset();
}

const decode::DecoderRegistry& GuiApp::decoderRegistry() const {
  static const decode::DecoderRegistry none;
  return decoders_ ? decoders_->registry() : none;
}

std::vector<gd::CapturedChannel> GuiApp::capturedChannelsFor(const SessionUi& ui) {
  std::vector<gd::CapturedChannel> out;
  out.reserve(ui.keptDeviceChannels.size());
  for (std::uint32_t device : ui.keptDeviceChannels) {
    // WHY: Use the channel's OWN name, not the user's: a decoder setup from a file may bind a channel by it.
    out.push_back({device, gui_capture::channelDisplayName(ui.channelNames, device)});
  }
  return out;
}

GuiApp::SessionUi* GuiApp::decodeRunOwner(std::uint64_t run) {
  for (auto& [sessionId, ui] : sessionUi_) {
    if (run != 0 && ui.decoderAnnotations.run() == run) return &ui;
  }
  return nullptr;
}

void GuiApp::letGoDecodes(SessionUi& ui) {
  ui.decoderRunner.reset();
  ui.laneView.reset();
  ++ui.laneViewSerial;
  ui.decoderAnnotations.captureLetGo();
}

void GuiApp::runDecodersForSession(gui_session::SessionId id, SessionUi& ui) {
  // WHY: Cancel the old run first: its queued results carry the OLD run id and are dropped, never misapplied.
  letGoDecodes(ui);
  startDecodeRun(id, ui);
}

void GuiApp::decodeSessionAgain(gui_session::SessionId id, SessionUi& ui) {
  ui.decoderRunner.reset();
  ui.laneView.reset();
  ++ui.laneViewSerial;
  ui.decoderAnnotations.setAsideForRecompute();
  startDecodeRun(id, ui);
  ui.decoderAnnotations.dropSetAside();
}

void GuiApp::startDecodeRun(gui_session::SessionId id, SessionUi& ui) {
  if (!ui.capture.kept() || !decoders_) {
    return;
  }
  using MadeView = std::expected<std::shared_ptr<const decode::SignalView>, std::string>;
  auto views = gd::signalViewSourceForOutcome(*ui.capture.kept());
  const MadeView view = views ? (*views)() : MadeView(std::unexpected(views.error()));
  if (!view) {
    const gui_session::Session* session = sessions_.find(id);
    logger_.logf(log::LogLevel::error, "decode", "{}: cannot decode this capture: {}",
                session != nullptr ? session->name : std::to_string(id), view.error());
    return;
  }
  if (auto laneView = gd::signalViewForOutcome(*ui.capture.kept())) {
    ui.laneView = std::move(*laneView);
    ++ui.laneViewSerial;
  }
  if (ui.decoders.empty()) return;

  const std::vector<gd::CapturedChannel> captured = capturedChannelsFor(ui);
  const decode::CaptureFacts facts{(*view)->sampleRateMilliHz(), (*view)->channelCount(), (*view)->sampleCount()};
  gd::ValidatedChain chain = gd::validateForRun(ui.decoders, decoderRegistry(), captured, facts);
  if (chain.plan.instances.empty()) return;
  const gui_session::Session* session = sessions_.find(id);
  const std::string name = session != nullptr ? session->name : std::to_string(id);

  const std::uint64_t run = nextRun_++;
  std::vector<gd::DecoderConfig::AnnotationFormatContext> formats;
  std::vector<gd::DerivationContext> derivations;
  formats.reserve(chain.configIndices.size());
  derivations.reserve(chain.configIndices.size());
  for (const std::size_t index : chain.configIndices) {
    formats.push_back(ui.decoders[index].annotationFormatContext(decoderRegistry()));
    derivations.push_back(
      gd::runDerivationContext(ui.decoders[index], decoderRegistry(), captured, facts.sample_rate_milli_hz));
  }
  ui.decoderAnnotations.beginRun(run, std::move(chain.configIndices), std::move(formats), std::move(derivations));
  const auto freeBytes = memoryProbe_->freePhysicalBytes();
  gd::DecodeMemory memory;
  memory.budgetBytes =
    gd::decoderBudgetBytes(gd::budgetMibFromEnvironment(), prefs_.decoderMemoryBudgetMib,
                           freeBytes ? std::optional<std::uint64_t>(*freeBytes) : std::nullopt);
  memory.heldBytes = ui.decoderAnnotations.heldBytes();
  logger_.logf(log::LogLevel::info, "decode", "{}: decoded results may hold {} MiB", name,
               memory.budgetBytes / gd::kMiB);
  auto capture = gd::runCaptureForOutcome(*ui.capture.kept());
  ui.decoderRunner = std::make_unique<gd::DecoderRunner>(
    run, std::move(*views), std::move(chain.plan), eventQueue_, std::move(memory), decoders_->threadSetup(),
    gd::ProgressClock{}, std::function<void(std::size_t)>{},
    capture ? std::optional<gd::RunCapture>(std::move(*capture)) : std::nullopt);
}

void GuiApp::decoderResultReceived(std::uint64_t run, std::size_t decoderIndex,
                                   const std::expected<std::shared_ptr<const gd::DecodedAnnotations>, std::string>& result) {
  SessionUi* ui = decodeRunOwner(run);
  if (ui == nullptr || !ui->decoderAnnotations.resultArrived(run, decoderIndex, result)) return;
  if (!result.has_value()) {
    logger_.logf(log::LogLevel::error, "decode", "decoder failed: {}", result.error());
  }
}

void GuiApp::decoderProgressReceived(std::uint64_t run, std::size_t decoderIndex, const gd::DecodeProgress& progress) {
  if (SessionUi* ui = decodeRunOwner(run)) ui->decoderAnnotations.progressArrived(run, decoderIndex, progress);
}

bool GuiApp::drawDecoderPicker(SessionUi& ui) {
  static const std::vector<gd::PickerEntry> entries = gd::decoderPickerEntries(decoderRegistry());
  const ImGuiStyle& style = ImGui::GetStyle();
  const float rowHeight = ImGui::GetTextLineHeightWithSpacing();
  const float em = ImGui::GetFontSize();
  bool added = false;

  const auto addPicked = [&](const std::string& id) {
    gd::addDecoder(ui.decoders, id, decoderRegistry(), &ui.nextDecoderColor);
    added = true;
  };

  ImGui::SetNextItemWidth(-1);
  const bool filterEntered = ImGui::InputTextWithHint("##decoderFilter", tr("Filter decoders"), ui.decoderFilter.data(),
                                                      ui.decoderFilter.size(), ImGuiInputTextFlags_EnterReturnsTrue);
  const std::vector<gd::PickerRow> rows = gd::pickerRows(entries, ui.decoderFilter.data());

  const float roomForList = ImGui::GetContentRegionAvail().y - 3.0f * rowHeight;
  const float listHeight = gd::clampPickerHeight(
    roomForList, gui_prefs::pickerHeightPixels(decoderPickerHeightCentiEm_, em), rowHeight);

  bool listEntered = false;
  if (ImGui::BeginChild("##decoderList", ImVec2(0, listHeight + 2.0f * style.WindowPadding.y), ImGuiChildFlags_Borders)) {
    int pushId = 0;
    for (const gd::PickerRow& row : rows) {
      if (row.separator) {
        ImGui::Separator();
        continue;
      }
      ImGui::PushID(pushId++);
      const bool picked = row.entry.id == ui.addDecoderPick;
      if (ImGui::Selectable(row.entry.display.c_str(), picked, ImGuiSelectableFlags_AllowDoubleClick)) {
        ui.addDecoderPick = row.entry.id;
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) addPicked(row.entry.id);
      }
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) {
        const std::string decoderLine = row.entry.version.empty() ? std::string(tr("Decoder gives no version"))
                                                                  : trf("Decoder version {}", row.entry.version);
        if (row.entry.plugin.empty()) {
          ImGui::SetTooltip("%s", decoderLine.c_str());
        } else {
          const std::string pluginLine =
            row.entry.pluginVersion.empty()
              ? trf("Decoder plugin {}, no version given", row.entry.plugin)
              : trf("Decoder plugin {}, version {}", row.entry.plugin, row.entry.pluginVersion);
          ImGui::SetTooltip("%s\n%s", decoderLine.c_str(), pluginLine.c_str());
        }
      }
      ImGui::PopID();
    }
    listEntered = ImGui::IsWindowFocused() && ImGui::IsKeyPressed(ImGuiKey_Enter);
  }
  ImGui::EndChild();

  if (filterEntered || listEntered) {
    const auto shown = [&](const std::string& id) {
      return std::any_of(rows.begin(), rows.end(), [&](const gd::PickerRow& r) { return !r.separator && r.entry.id == id; });
    };
    if (shown(ui.addDecoderPick)) {
      addPicked(ui.addDecoderPick);
    } else {
      const auto first = std::find_if(rows.begin(), rows.end(), [](const gd::PickerRow& r) { return !r.separator; });
      if (first != rows.end()) addPicked(first->entry.id);
    }
    if (filterEntered) ImGui::SetKeyboardFocusHere(-1);
  }

  const bool canAdd = !ui.addDecoderPick.empty() && std::any_of(rows.begin(), rows.end(), [&](const gd::PickerRow& r) {
    return !r.separator && r.entry.id == ui.addDecoderPick;
  });
  ImGui::BeginDisabled(!canAdd);
  if (ImGui::SmallButton(tr("Add decoder"))) addPicked(ui.addDecoderPick);
  ImGui::EndDisabled();

  ImGui::InvisibleButton("##decoderSplitter", ImVec2(-1, (std::max)(4.0f, em * 0.4f)));
  if (ImGui::IsItemHovered() || ImGui::IsItemActive()) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
  const ImVec2 lineMin = ImGui::GetItemRectMin();
  const ImVec2 lineMax = ImGui::GetItemRectMax();
  const float lineThickness = (std::max)(1.0f, std::round(em / kFontSizeBase));
  const float lineTop = std::floor((lineMin.y + lineMax.y) * 0.5f - lineThickness * 0.5f);
  const SplitterState splitterState = ImGui::IsItemActive()    ? SplitterState::Dragged
                                      : ImGui::IsItemHovered() ? SplitterState::Hovered
                                                               : SplitterState::Rest;
  ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(lineMin.x, lineTop), ImVec2(lineMax.x, lineTop + lineThickness),
                                      ImGui::GetColorU32(splitterLineColor(ImGui::GetStyle(), splitterState)));
  if (ImGui::IsItemActive() && ImGui::IsMousePosValid()) {
    const float dragged = gd::clampPickerHeight(roomForList, listHeight + ImGui::GetIO().MouseDelta.y, rowHeight);
    decoderPickerHeightCentiEm_ = gui_prefs::pickerHeightFromPixels(dragged, em);
  }
  return added;
}

GuiApp::DecoderChannelChoices GuiApp::decoderChannelChoices(const SessionUi& ui) {
  DecoderChannelChoices choices;
  choices.captured = capturedChannelsFor(ui);
  choices.labels.push_back(tr("(none)"));
  for (const gd::CapturedChannel& c : choices.captured) {
    choices.labels.push_back(gui_capture::channelChoiceLabel(ui.channelUserNames, ui.channelNames, c.deviceChannel));
  }
  return choices;
}

GuiApp::DecoderSettingsChange GuiApp::drawDecoderSettings(SessionUi& ui, std::size_t index,
                                                          const DecoderChannelChoices& channels) {
  DecoderSettingsChange change;
  if (index >= ui.decoders.size()) return change;
  const decode::DecoderRegistry& registry = decoderRegistry();
  const bool dark = darkInUse();
  const ImGuiStyle& style = ImGui::GetStyle();
  gd::DecoderConfig& config = ui.decoders[index];
  auto decoder = registry.create(config.decoderName());
  const float rowLeft = ImGui::GetCursorScreenPos().x;
  const float rowWidth = ImGui::GetContentRegionAvail().x;
  const float fieldX = std::floor(rowWidth * 0.45f);

  const auto label = [&](const std::string& text, bool bold, const std::string& tooltip) {
    ImGui::AlignTextToFramePadding();
    if (bold) {
      boldText(text);
    } else {
      ImGui::TextUnformatted(text.c_str());
    }
    if (!tooltip.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) {
      ImGui::BeginTooltip();
      ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
      ImGui::TextUnformatted(tooltip.c_str());
      ImGui::PopTextWrapPos();
      ImGui::EndTooltip();
    }
    if (ImGui::CalcTextSize(text.c_str()).x + style.ItemSpacing.x <= fieldX) {
      ImGui::SameLine();
      ImGui::SetCursorScreenPos(ImVec2(rowLeft + fieldX, ImGui::GetCursorScreenPos().y));
    }
    ImGui::SetNextItemWidth(-FLT_MIN);
  };
  const auto fieldMessage = [&](std::string_view field) -> std::optional<std::string> {
    const auto found = ui.decoderFieldMessages.find(fieldMessageKey(config.id(), field));
    if (found == ui.decoderFieldMessages.end()) return std::nullopt;
    return found->second;
  };

  showFieldError(config.errorFor(""), dark);
  showFieldError(config.errorFor(gd::idFieldKey()), dark);
  if (!decoder.has_value()) return change;

  const std::vector<decode::ChannelSpec> channelSpecs = (*decoder)->channels();
  if (!channelSpecs.empty()) ImGui::Separator();
  for (const decode::ChannelSpec& spec : channelSpecs) {
    ImGui::PushID(spec.name.c_str());
    const gd::FieldLine line = gd::channelLine(spec);
    label(line.bold ? line.label + " *" : line.label, line.bold, line.tooltip);
    const std::optional<std::uint32_t> bound = config.channelBinding(spec.name);
    int choice = 0;
    if (bound) {
      for (std::size_t c = 0; c < channels.captured.size(); ++c) {
        if (channels.captured[c].deviceChannel == *bound) choice = static_cast<int>(c) + 1;
      }
    }
    if (comboOf("##ch", choice, channels.labels)) {
      config.setChannelBinding(spec.name, choice == 0 ? std::nullopt
                                                      : std::optional<std::uint32_t>(
                                                          channels.captured[static_cast<std::size_t>(choice) - 1].deviceChannel));
      change.setup = true;
    }
    showFieldError(config.errorFor(gd::channelFieldKey(spec.name)), dark);
    ImGui::PopID();
  }
  if (gd::anyRequiredChannel(channelSpecs)) {
    ImGui::TextDisabled("%s", tr("* Required channels"));
  }

  const std::vector<decode::InputSpec> inputSpecs = (*decoder)->inputs();
  if (!inputSpecs.empty()) ImGui::Separator();
  for (const decode::InputSpec& spec : inputSpecs) {
    ImGui::PushID(spec.name.c_str());
    const std::optional<std::string> bound = config.inputBinding(spec.name);
    std::vector<std::string> bindings;
    std::vector<std::optional<std::string>> notDesignedFor;
    for (const gd::BindingCandidate& c : gd::dropdownCandidates(ui.decoders, index, spec.name, registry)) {
      bindings.push_back(c.binding);
      notDesignedFor.push_back(c.notDesignedFor);
    }
    if (bound && std::find(bindings.begin(), bindings.end(), *bound) == bindings.end()) {
      bindings.push_back(*bound);
      notDesignedFor.emplace_back();
    }
    std::vector<std::string> items{tr("(none)")};
    for (std::size_t b = 0; b < bindings.size(); ++b) {
      std::string text = gd::bindingLabel(bindings[b], ui.decoders, registry);
      if (notDesignedFor[b]) text += std::string(" ") + tr("(not designed for)");
      items.push_back(std::move(text));
    }
    int choice = 0;
    if (bound) {
      choice = static_cast<int>(std::find(bindings.begin(), bindings.end(), *bound) - bindings.begin()) + 1;
    }
    label(gd::inputLabel(spec), false, gd::inputTooltip(spec));
    if (comboOf("##in", choice, items)) {
      config.setInputBinding(spec.name, choice == 0 ? std::nullopt
                                                    : std::optional<std::string>(
                                                        bindings[static_cast<std::size_t>(choice) - 1]));
      change.setup = true;
    }
    std::optional<std::string> inputError = config.errorFor(gd::inputFieldKey(spec.name));
    if (!inputError && choice > 0) inputError = notDesignedFor[static_cast<std::size_t>(choice) - 1];
    showFieldError(inputError, dark);
    ImGui::PopID();
  }

  const std::vector<decode::OptionSpec> optionSpecs = (*decoder)->options();
  if (!optionSpecs.empty()) ImGui::Separator();
  for (const decode::OptionSpec& spec : optionSpecs) {
    // WHY: The GUI forces this option to no for every run: one global setting shows and hides the bits rows.
    if (gd::isDerivedRowEnableOption(registry, config.decoderName(), spec.name)) continue;
    ImGui::PushID(spec.name.c_str());
    const gd::OptionWidget widget = gd::widgetFor(spec);
    const std::string value = config.optionValue(spec.name, spec);
    const auto setValue = [&](const std::string& text) {
      if (text == value) return;
      config.setOptionValue(spec.name, text);
      change.setup = true;
    };
    label(gd::optionLine(spec).label, false, gd::optionLine(spec).tooltip);
    switch (widget.kind) {
    case gd::WidgetKind::Checkbox: {
      bool checked = gd::checkboxState(value).value_or(false);
      if (ImGui::Checkbox("##opt", &checked)) setValue(gd::checkboxText(checked));
      break;
    }
    case gd::WidgetKind::Radio: {
      const std::optional<std::size_t> picked = gd::choiceIndex(widget, value);
      const float rowLeftX = ImGui::GetCursorScreenPos().x;
      const float rowRightX = rowLeftX + ImGui::GetContentRegionAvail().x;
      for (std::size_t c = 0; c < widget.choices.size(); ++c) {
        const std::string text = gd::optionValueText(spec, widget.choices[c]);
        const float width = ImGui::GetFrameHeight() + style.ItemInnerSpacing.x + ImGui::CalcTextSize(text.c_str()).x;
        if (c > 0) {
          if (ImGui::GetItemRectMax().x + style.ItemSpacing.x + width <= rowRightX) {
            ImGui::SameLine();
          } else {
            ImGui::SetCursorScreenPos(ImVec2(rowLeftX, ImGui::GetCursorScreenPos().y));
          }
        }
        ImGui::PushID(static_cast<int>(c));
        if (ImGui::RadioButton(text.c_str(), picked == c)) setValue(widget.choices[c]);
        ImGui::PopID();
      }
      if (!picked) ImGui::TextDisabled("%s", value.c_str());
      break;
    }
    case gd::WidgetKind::Dropdown:
      if (ImGui::BeginCombo("##opt",gd::optionValueText(spec, value).c_str())) {
        for (const std::string& c : widget.choices) {
          const bool selected = c == value;
          if (ImGui::Selectable(gd::optionValueText(spec, c).c_str(), selected)) setValue(c);
          if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
      }
      break;
    case gd::WidgetKind::Slider: {
      const std::size_t steps = gd::sliderStepCount(widget);
      const std::optional<std::size_t> position = gd::sliderPosition(widget, value);
      int at = static_cast<int>(position.value_or(0));
      const std::string format = literalFormat(position ? gd::sliderText(widget, *position) : value);
      if (ImGui::SliderInt("##opt", &at, 0, static_cast<int>(steps) - 1, format.c_str(), ImGuiSliderFlags_NoInput)) {
        setValue(gd::sliderText(widget, static_cast<std::size_t>((std::max)(at, 0))));
      }
      break;
    }
    case gd::WidgetKind::NumberBox: {
      gd::OptionTextBuffer& buffer = gd::optionTextBuffer(ui.decoderOptionBuffers, ui.decoders, index, spec.name, value);
      const float arrow = ImGui::GetFrameHeight();
      ImGui::SetNextItemWidth((std::max)(arrow, ImGui::GetContentRegionAvail().x - 2.0f * (arrow + style.ItemInnerSpacing.x)));
      const std::string key = fieldMessageKey(config.id(), spec.name);
      if (gui_text_field::committedInputText("##opt", buffer.data(), buffer.size(), ImGuiInputTextFlags_CharsDecimal) !=
          gui_text_field::Commit::None) {
        const auto clamped = gd::clampedIntegerText(spec, buffer.data());
        if (clamped) {
          ui.decoderFieldMessages.erase(key);
          setValue(*clamped);
          copyToBuffer(buffer, *clamped);
        } else {
          ui.decoderFieldMessages[key] = clamped.error();
          copyToBuffer(buffer, value);
        }
      }
      for (const auto& [text, delta] : {std::pair{"-", std::int64_t{-1}}, std::pair{"+", std::int64_t{1}}}) {
        ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
        if (ImGui::Button(text, ImVec2(arrow, arrow))) {
          const std::string stepped = gd::steppedIntegerText(spec, value, delta);
          ui.decoderFieldMessages.erase(key);
          setValue(stepped);
          copyToBuffer(buffer, stepped);
        }
      }
      showFieldError(fieldMessage(spec.name), dark);
      break;
    }
    case gd::WidgetKind::Text: {
      gd::OptionTextBuffer& buffer = gd::optionTextBuffer(ui.decoderOptionBuffers, ui.decoders, index, spec.name, value);
      if (gui_text_field::committedInputText("##opt", buffer.data(), buffer.size()) != gui_text_field::Commit::None) {
        config.setOptionValue(spec.name, buffer.data());
        change.setup = true;
      }
      break;
    }
    }
    showFieldError(config.errorFor(gd::optionFieldKey(spec.name)), dark);
    ImGui::PopID();
  }
  return change;
}

void GuiApp::applyDecoderSettingsChange(gui_session::SessionId id, SessionUi& ui, const DecoderSettingsChange& change) {
  if (change.setup) {
    gd::pruneOptionTextBuffers(ui.decoderOptionBuffers, ui.decoders);
    ui.capture.markDecodersEdited();
    decodeSessionAgain(id, ui);
  } else if (change.presentation) {
    ui.capture.markDecodersEdited();
  }
}

void GuiApp::drawDecodersProperties(SessionUi& ui) {
  ImGui::SeparatorText(tr("Decoders"));
  const std::vector<std::string> names = decoderRegistry().names();
  const DecoderChannelChoices channels = decoderChannelChoices(ui);

  bool added = false;
  int removeIndex = -1;
  DecoderSettingsChange change;

  if (names.empty()) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", tr("No decoders yet"));
    ImGui::PopStyleColor();
  } else {
    added = drawDecoderPicker(ui);
  }

  ImGui::BeginChild("##addedDecoders", ImVec2(0, 0), ImGuiChildFlags_None);
  for (std::size_t i = 0; i < ui.decoders.size(); ++i) {
    // WHY: Key open state by instance id, not index (a removal shifts indexes); '###' keeps state across label changes.
    const std::string idBefore = ui.decoders[i].id();
    ImGui::PushID(idBefore.c_str());
    const std::string header = gd::decoderBarName(ui.decoders, i, decoderRegistry()) + "###decoder";
    const ImGuiID headerId = ImGui::GetID("###decoder");
    ImGuiWindow* const window = ImGui::GetCurrentWindow();
    const float removeWidth = ImGui::CalcTextSize(tr("Remove")).x + 2.0f * ImGui::GetStyle().FramePadding.x;
    const float rightEdge = window->WorkRect.Max.x;
    window->WorkRect.Max.x = rightEdge - removeWidth - ImGui::GetStyle().ItemInnerSpacing.x;
    ImGui::SetNextItemOpen(false, ImGuiCond_Once);
    const bool open = ImGui::CollapsingHeader(header.c_str(), ImGuiTreeNodeFlags_AllowOverlap);
    window->WorkRect.Max.x = rightEdge;
    const float headerY = ImGui::GetItemRectMin().y;
    ImGui::SameLine(0.0f, 0.0f);
    ImGui::SetCursorScreenPos(ImVec2(rightEdge - removeWidth, headerY));
    if (ImGui::SmallButton(tr("Remove"))) {
      removeIndex = static_cast<int>(i);
      ImGui::GetStateStorage()->SetInt(headerId, -1);
    }
    if (open) {
      const DecoderSettingsChange one = drawDecoderSettings(ui, i, channels);
      change.setup = change.setup || one.setup;
      change.presentation = change.presentation || one.presentation;
    }
    ImGui::PopID();
    if (ui.decoders[i].id() != idBefore) {
      ImGui::PushID(ui.decoders[i].id().c_str());
      ImGui::GetStateStorage()->SetInt(ImGui::GetID("###decoder"), open ? 1 : 0);
      ImGui::PopID();
    }
  }
  if (removeIndex >= 0) {
    ui.decoders.erase(ui.decoders.begin() + removeIndex);
    ui.laneOrder.removeDecoder(static_cast<std::size_t>(removeIndex));
    ui.decoderAnnotations.removeDecoder(static_cast<std::size_t>(removeIndex));
    change.setup = true;
  }

  ImGui::EndChild();

  change.setup = change.setup || added;
  if (!change.setup && !change.presentation) return;
  for (auto& [sessionId, candidate] : sessionUi_) {
    if (&candidate == &ui) {
      applyDecoderSettingsChange(sessionId, ui, change);
      break;
    }
  }
}

}
