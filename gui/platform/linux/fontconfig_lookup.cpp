/*
 * SPDX-License-Identifier: MIT
 */

#include "fontconfig_lookup.h"

#include <dlfcn.h>

#include <mutex>
#include <optional>
#include <utility>

namespace bitoscope::gui::linux_platform {

namespace {

struct FcConfig;
struct FcPattern;
using FcChar8 = unsigned char;
using FcBool = int;
enum FcResult { FcResultMatch = 0 };
enum FcMatchKind { FcMatchPattern = 0 };

struct Fontconfig {
  FcConfig* (*initLoadConfigAndFonts)() = nullptr;
  void (*configDestroy)(FcConfig*) = nullptr;
  FcPattern* (*patternCreate)() = nullptr;
  void (*patternDestroy)(FcPattern*) = nullptr;
  FcBool (*patternAddString)(FcPattern*, const char*, const FcChar8*) = nullptr;
  FcBool (*configSubstitute)(FcConfig*, FcPattern*, FcMatchKind) = nullptr;
  void (*defaultSubstitute)(FcPattern*) = nullptr;
  FcPattern* (*fontMatch)(FcConfig*, FcPattern*, FcResult*) = nullptr;
  FcResult (*patternGetString)(const FcPattern*, const char*, int, FcChar8**) = nullptr;
  FcResult (*patternGetInteger)(const FcPattern*, const char*, int, int*) = nullptr;
};

template <typename F>
bool resolve(void* library, const char* name, F& function) {
  function = reinterpret_cast<F>(dlsym(library, name));
  return function != nullptr;
}

const Fontconfig* fontconfig() {
  static std::once_flag once;
  static Fontconfig fc;
  static bool complete = false;
  std::call_once(once, [] {
    void* library = dlopen("libfontconfig.so.1", RTLD_LAZY | RTLD_LOCAL);
    if (library == nullptr) {
      return;
    }
    complete = resolve(library, "FcInitLoadConfigAndFonts", fc.initLoadConfigAndFonts) &&
               resolve(library, "FcConfigDestroy", fc.configDestroy) &&
               resolve(library, "FcPatternCreate", fc.patternCreate) &&
               resolve(library, "FcPatternDestroy", fc.patternDestroy) &&
               resolve(library, "FcPatternAddString", fc.patternAddString) &&
               resolve(library, "FcConfigSubstitute", fc.configSubstitute) &&
               resolve(library, "FcDefaultSubstitute", fc.defaultSubstitute) &&
               resolve(library, "FcFontMatch", fc.fontMatch) &&
               resolve(library, "FcPatternGetString", fc.patternGetString) &&
               resolve(library, "FcPatternGetInteger", fc.patternGetInteger);
  });
  return complete ? &fc : nullptr;
}

}

std::expected<FontMatch, std::string> matchFontFamily(std::string_view family) {
  const Fontconfig* fc = fontconfig();
  if (fc == nullptr) {
    return std::unexpected("fontconfig (libfontconfig.so.1) could not be loaded");
  }
  FcConfig* config = fc->initLoadConfigAndFonts();
  if (config == nullptr) {
    return std::unexpected("fontconfig could not load its configuration");
  }
  std::optional<FontMatch> match;
  std::string problem;
  FcPattern* pattern = fc->patternCreate();
  const std::string name(family);
  // WHY: The family is set as a value, not parsed as a fontconfig name, so '-' or ':' in it mean nothing.
  if (pattern != nullptr &&
      fc->patternAddString(pattern, "family", reinterpret_cast<const FcChar8*>(name.c_str())) != 0) {
    fc->configSubstitute(config, pattern, FcMatchPattern);
    fc->defaultSubstitute(pattern);
    FcResult result = FcResultMatch;
    if (FcPattern* found = fc->fontMatch(config, pattern, &result)) {
      FcChar8* file = nullptr;
      FcChar8* matchedFamily = nullptr;
      int index = 0;
      if (fc->patternGetString(found, "file", 0, &file) == FcResultMatch && file != nullptr) {
        match = FontMatch{std::filesystem::path(reinterpret_cast<const char*>(file)), 0, {}};
        if (fc->patternGetInteger(found, "index", 0, &index) == FcResultMatch) {
          match->index = index;
        }
        if (fc->patternGetString(found, "family", 0, &matchedFamily) == FcResultMatch &&
            matchedFamily != nullptr) {
          match->family = reinterpret_cast<const char*>(matchedFamily);
        }
      } else {
        problem = "fontconfig's match has no file";
      }
      fc->patternDestroy(found);
    } else {
      problem = "fontconfig matched nothing";
    }
  } else {
    problem = "fontconfig could not make pattern";
  }
  if (pattern != nullptr) {
    fc->patternDestroy(pattern);
  }
  fc->configDestroy(config);
  if (!match) {
    return std::unexpected(std::move(problem));
  }
  return *match;
}

}
