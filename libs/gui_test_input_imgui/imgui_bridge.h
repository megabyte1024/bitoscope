/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <span>
#include <vector>

#include <imgui.h>

#include <reply_format.h>
#include <sequencer.h>

namespace bitoscope::gui_test_input {

ImGuiKey toImGuiKey(KeyId key);

void blockRealInput(ImGuiIO& io);

void applyEvents(ImGuiIO& io, std::span<const InputEvent> events);

CoordinateScale scaleOf(const ImGuiIO& io);

std::vector<WindowInfo> collectWindows(ImGuiContext& context);

HoverInfo collectHover(ImGuiContext& context);

InfoReply collectInfo(ImGuiContext& context);

}
