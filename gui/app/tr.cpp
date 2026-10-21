/*
 * SPDX-License-Identifier: MIT
 */

#include "tr.h"

namespace bitoscope::gui {

namespace {

const i18n::Translator* installed = nullptr;

const i18n::Translator& sourceText() {
  static const i18n::Translator none;
  return none;
}

}

void setTranslator(const i18n::Translator* translator) {
  installed = translator;
}

const i18n::Translator& currentTranslator() {
  return installed != nullptr ? *installed : sourceText();
}

const char* tr(const char* text) {
  return currentTranslator().tr(text);
}

const char* trc(const char* context, const char* text) {
  return currentTranslator().trc(context, text);
}

const char* trn(const char* one, const char* many, unsigned long n) {
  return currentTranslator().trn(one, many, n);
}

std::string withId(const char* label, const char* id) {
  std::string out(label);
  out += "###";
  out += id;
  return out;
}

}
