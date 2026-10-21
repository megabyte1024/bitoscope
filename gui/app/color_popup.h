/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <array>
#include <functional>
#include <optional>

#include <color_choice.h>

namespace bitoscope::gui {

using ColorDraft = std::array<float, 4>;

void loadColorDraft(ColorDraft& draft, const std::optional<waveform_view::ColorChoice>& current);

bool colorChoiceBody(ColorDraft& draft, const std::function<void(waveform_view::ColorChoice)>& pick, bool canReset,
                     const char* resetId, const char* resetTooltip, bool dark);

}
