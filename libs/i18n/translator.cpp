/*
 * SPDX-License-Identifier: MIT
 */

#include "translator.h"

#include <utility>

namespace bitoscope::i18n {

void Translator::install(std::shared_ptr<const Catalog> catalog, std::string language) {
  catalog_ = std::move(catalog);
  language_ = std::move(language);
}

// WHY: Catalog::translate() returns a whole NUL-terminated string either way, so .data() is a C string.

const char* Translator::tr(const char* msgid) const {
  return catalog_ ? catalog_->translate(msgid).data() : msgid;
}

const char* Translator::trc(const char* context, const char* msgid) const {
  return catalog_ ? catalog_->translate(context, msgid).data() : msgid;
}

const char* Translator::trn(const char* msgid, const char* msgidPlural, unsigned long n) const {
  if (catalog_) {
    return catalog_->translatePlural(msgid, msgidPlural, n).data();
  }
  return n == 1 ? msgid : msgidPlural;
}

std::string Translator::formatWithFallback(std::string_view translated, std::string_view source,
                                           std::format_args args) {
  try {
    return std::vformat(translated, args);
  } catch (const std::format_error&) {
  }
  try {
    return std::vformat(source, args);
  } catch (const std::format_error&) {
  }
  return std::string(source);
}

}
