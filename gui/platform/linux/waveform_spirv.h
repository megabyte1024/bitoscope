/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <span>

namespace bitoscope::gui::linux_platform {

std::span<const std::uint32_t> waveformVertexSpirv();
std::span<const std::uint32_t> waveformFragmentSpirv();

}
