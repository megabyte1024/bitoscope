/*
 * SPDX-License-Identifier: MIT
 */

#include "annotation_role.h"

#include <array>
#include <cstddef>

namespace bitoscope::decode {

namespace {

constexpr std::array<std::string_view, 10> kNames{"none",  "data",    "start",   "stop", "ok",
                                                  "error", "control", "address", "bit",  "info"};

}

std::string_view annotationRoleName(AnnotationRole role) {
  const auto index = static_cast<std::size_t>(role);
  return index < kNames.size() ? kNames[index] : kNames[0];
}

std::optional<AnnotationRole> annotationRoleNamed(std::string_view name) {
  for (std::size_t i = 0; i < kNames.size(); ++i) {
    if (kNames[i] == name) return static_cast<AnnotationRole>(i);
  }
  return std::nullopt;
}

}
