/*
 * SPDX-License-Identifier: MIT
 */

#include "gui_locale.h"

#include <algorithm>
#include <cstdint>
#include <format>
#include <fstream>
#include <iterator>
#include <system_error>

namespace bitoscope::gui {

namespace {

std::optional<std::string> readWholeFile(const std::filesystem::path& path,
                                         std::vector<std::uint8_t>& out) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return std::format("cannot open {}", path.string());
  }
  try {
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  } catch (const std::bad_alloc&) {
    return std::format("out of memory reading {}", path.string());
  }
  if (in.bad()) {
    return std::format("cannot read {}", path.string());
  }
  return std::nullopt;
}

}

std::vector<std::string> listCatalogs(const std::filesystem::path& localeDir) {
  std::vector<std::string> names;
  std::error_code ec;
  for (const auto& entry : std::filesystem::directory_iterator(localeDir, ec)) {
    if (entry.is_regular_file(ec) && entry.path().extension() == ".mo") {
      names.push_back(entry.path().stem().string());
    }
  }
  std::sort(names.begin(), names.end());
  return names;
}

LoadedLanguage loadLanguage(const std::filesystem::path& localeDir, std::string language) {
  LoadedLanguage loaded;
  if (language.empty() || language == "en") {
    return loaded;
  }
  const std::filesystem::path path = (localeDir / (language + ".mo")).make_preferred();
  std::vector<std::uint8_t> bytes;
  if (auto problem = readWholeFile(path, bytes)) {
    loaded.problem = std::format("Language {} not loaded: {}", language, *problem);
    return loaded;
  }
  auto catalog = i18n::Catalog::parse(bytes);
  if (!catalog) {
    loaded.problem = std::format("Language {} not loaded: {}: {}", language, path.string(),
                                 catalog.error());
    return loaded;
  }
  loaded.language = std::move(language);
  loaded.catalog = std::make_shared<const i18n::Catalog>(std::move(*catalog));
  return loaded;
}

}
