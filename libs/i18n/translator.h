/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

// WHY: tr() pointers stay valid only until the next install(): swap catalogs between frames, never during one.

#include <format>
#include <memory>
#include <string>
#include <string_view>

#include "catalog.h"

namespace bitoscope::i18n {

class Translator {
 public:
  Translator() = default;

  void install(std::shared_ptr<const Catalog> catalog, std::string language);

  const std::string& language() const { return language_; }
  bool hasCatalog() const { return catalog_ != nullptr; }

  const char* tr(const char* msgid) const;
  const char* trc(const char* context, const char* msgid) const;
  const char* trn(const char* msgid, const char* msgidPlural, unsigned long n) const;

  template <typename... Args>
  std::string format(const char* msgid, const Args&... args) const {
    return formatWithFallback(tr(msgid), msgid, std::make_format_args(args...));
  }

  template <typename... Args>
  std::string formatN(const char* msgid, const char* msgidPlural, unsigned long n,
                      const Args&... args) const {
    return formatWithFallback(trn(msgid, msgidPlural, n), n == 1 ? msgid : msgidPlural,
                              std::make_format_args(args...));
  }

  static std::string formatWithFallback(std::string_view translated, std::string_view source,
                                        std::format_args args);

 private:
  std::shared_ptr<const Catalog> catalog_;
  std::string language_ = "en";
};

}
