/*
 * SPDX-License-Identifier: MIT
 */

#include <cstdint>
#include <cstring>

extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

#include "binding.h"
#include "bitoscope_decoder.h"
#include "embedded_script.h"

namespace bitoscope::lua_gateway::binding {

namespace {

struct LuaApi {
  int (*level)(bitoscope_decode_input*, uint32_t, uint64_t);
  int (*next_edge)(bitoscope_decode_input*, uint32_t, uint64_t, int, uint64_t*);
  int (*next_edge_limit)(bitoscope_decode_input*, uint32_t, uint64_t, int, uint64_t, uint64_t*);
  int (*next_change2)(bitoscope_decode_input*, uint32_t, uint32_t, uint64_t, uint64_t*, uint32_t*);
  int (*next_change2_limit)(bitoscope_decode_input*, uint32_t, uint32_t, uint64_t, uint64_t, uint64_t*, uint32_t*);
  int (*bits)(bitoscope_decode_input*, uint32_t, uint64_t, uint32_t, uint64_t*);
  int (*sample_at)(bitoscope_decode_input*, uint32_t, uint64_t, uint64_t, uint64_t, uint64_t, uint32_t, uint64_t*);
  int (*clocked)(bitoscope_decode_input*, uint32_t, uint32_t, uint64_t, int, uint32_t, uint64_t, uint64_t*, uint64_t*);
  uint64_t (*sample_count)(bitoscope_decode_input*, int*);
  int (*release_before)(bitoscope_decode_input*, uint64_t);
  const char* (*decode_error)(const bitoscope_decode_input*);
  int (*emit)(bitoscope_annotations*, uint64_t, uint64_t, uint32_t, uint32_t, uint64_t);
  int (*put)(bitoscope_annotations*, uint32_t, uint64_t, uint64_t, uint32_t, uint64_t, uint32_t);
  int (*next_item)(bitoscope_decode_input*, uint32_t, bitoscope_stream_item*);
  int (*settle)(bitoscope_annotations*, uint64_t);
  uint64_t (*settle_due)(const bitoscope_annotations*);
  int (*emit_bytes)(bitoscope_annotations*, uint64_t, uint64_t, uint32_t, uint32_t, const uint8_t*, size_t);
};

const LuaApi kApi = {
    &bitoscope_level,       &bitoscope_next_edge,     &bitoscope_next_edge_limit,
    &bitoscope_next_change2, &bitoscope_next_change2_limit, &bitoscope_bits,
    &bitoscope_sample_at,   &bitoscope_clocked,       &bitoscope_sample_count,
    &bitoscope_release_before, &bitoscope_decode_error, &bitoscope_emit,
    &bitoscope_put,         &bitoscope_next_item,       &bitoscope_settle,
    &bitoscope_settle_due,  &bitoscope_emit_bytes,
};

constexpr const char* kMakeKey = "bitoscope.lua_gateway.sig_ffi";

const EmbeddedScript* hostScript(const char* name) {
  for (const EmbeddedScript& s : hostScripts()) {
    if (std::strcmp(s.name, name) == 0) return &s;
  }
  return nullptr;
}

}

int prepare(lua_State* L) {
  const EmbeddedScript* sig = hostScript("sig_ffi.lua");
  if (sig == nullptr) return luaL_error(L, "gateway was built without sig_ffi.lua");
  if (luaL_loadbufferx(L, sig->text, sig->size, "@sig_ffi.lua", "t") != LUA_OK) return lua_error(L);
  lua_call(L, 0, 1);
  if (lua_type(L, -1) != LUA_TFUNCTION) return luaL_error(L, "sig_ffi.lua did not return function");
  lua_setfield(L, LUA_REGISTRYINDEX, kMakeKey);
  return 0;
}

int pushDecodeArguments(lua_State* L) {
  lua_getfield(L, LUA_REGISTRYINDEX, kMakeKey);
  lua_pushlightuserdata(L, const_cast<LuaApi*>(&kApi));
  lua_pushvalue(L, 1);
  lua_pushvalue(L, 2);
  lua_pushvalue(L, 3);
  lua_pushvalue(L, 4);
  lua_pushvalue(L, 5);
  lua_call(L, 6, 3);
  return 3;
}

}
