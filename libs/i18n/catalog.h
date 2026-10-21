/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "plural_rule.h"

namespace bitoscope::i18n {

class Catalog {
 public:
  static std::expected<Catalog, std::string> parse(std::span<const uint8_t> data);

  std::string_view translate(std::string_view msgid) const;

  std::string_view translate(std::string_view context, std::string_view msgid) const;

  std::string_view translatePlural(std::string_view msgid, std::string_view msgid_plural,
                                    unsigned long n) const;

  Catalog(Catalog&&) noexcept = default;
  Catalog& operator=(Catalog&&) noexcept = default;
  Catalog(const Catalog&) = delete;
  Catalog& operator=(const Catalog&) = delete;
  ~Catalog() = default;

 private:
  Catalog() = default;

  struct Entry {
    std::vector<std::string> forms;
  };

  std::unordered_map<std::string, Entry> entries_;
  PluralRule plural_rule_ = PluralRule::germanicDefault();
};

std::expected<Catalog, std::string> loadCatalogFile(std::string_view path);

}
