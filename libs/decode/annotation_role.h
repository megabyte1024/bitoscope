/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <optional>
#include <string_view>

#include "decoder.h"

namespace bitoscope::decode {

std::string_view annotationRoleName(AnnotationRole role);

std::optional<AnnotationRole> annotationRoleNamed(std::string_view name);

}
