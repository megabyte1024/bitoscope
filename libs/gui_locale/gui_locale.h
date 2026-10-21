/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <catalog.h>

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace bitoscope::gui {

inline constexpr std::string_view kLocaleDir = "locale";

struct LoadedLanguage {
  std::string language = "en";
  std::shared_ptr<const i18n::Catalog> catalog;
  std::optional<std::string> problem;
};

std::vector<std::string> listCatalogs(const std::filesystem::path& localeDir);

LoadedLanguage loadLanguage(const std::filesystem::path& localeDir, std::string language);

}
