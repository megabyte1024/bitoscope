/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstddef>
#include <span>

namespace bitoscope::lua_gateway {

struct EmbeddedScript {
  const char* name;
  const char* text;
  std::size_t size;
};

std::span<const EmbeddedScript> builtinScripts();

std::span<const EmbeddedScript> hostScripts();

}
