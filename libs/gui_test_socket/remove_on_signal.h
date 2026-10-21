/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <expected>
#include <string>
#include <string_view>
#include <vector>

namespace bitoscope::gui_test_socket {

inline constexpr std::size_t kMaxRemovalPath = 255;

std::expected<std::vector<int>, std::string> armFileRemovalOnSignal(std::string_view path);

void disarmFileRemovalOnSignal();

}
