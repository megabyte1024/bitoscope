/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <string>
#include <string_view>

namespace bitoscope::gui_save_name {

bool hasFileExtension(std::string_view fileName);

std::string withSessionExtension(std::string_view path);

}
