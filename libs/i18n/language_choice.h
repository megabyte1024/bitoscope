/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <span>
#include <string>
#include <string_view>

namespace bitoscope::i18n {

std::string chooseLanguage(std::span<const std::string> preferred_tags,
              std::span<const std::string> available_catalogs);

inline constexpr std::string_view kSourceLanguage = "en";

std::string resolveLanguage(std::string_view preference, std::span<const std::string> preferred_tags,
                            std::span<const std::string> available_catalogs);

}
