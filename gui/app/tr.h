/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <translator.h>

#include <string>

namespace bitoscope::gui {

void setTranslator(const i18n::Translator* translator);
const i18n::Translator& currentTranslator();

const char* tr(const char* text);
const char* trc(const char* context, const char* text);
const char* trn(const char* one, const char* many, unsigned long n);

template <typename... Args>
std::string trf(const char* format, const Args&... args) {
  return currentTranslator().format(format, args...);
}

template <typename... Args>
std::string trfn(const char* one, const char* many, unsigned long n, const Args&... args) {
  return currentTranslator().formatN(one, many, n, args...);
}

std::string withId(const char* label, const char* id);

}
