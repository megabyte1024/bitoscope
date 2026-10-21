/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>

struct lua_State;

namespace bitoscope::lua_gateway::binding {

int prepare(lua_State* L);

int pushDecodeArguments(lua_State* L);

}
