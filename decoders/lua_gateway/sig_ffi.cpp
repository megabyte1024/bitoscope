/*
 * SPDX-License-Identifier: MIT
 */

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <string>

extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

#include "binding.h"
#include "bitoscope_decoder.h"
#include "bitoscope_decoder_cpp.h"
#include "bitoscope_decoder_layers.h"
#include "embedded_script.h"

namespace bitoscope::lua_gateway::binding {

namespace {

struct LuaApi {
  int (*level)(bitoscope_decode_input*, uint32_t, uint64_t, int*);
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
  int (*changes)(bitoscope_decode_input*, const bitoscope_watched_line*, uint32_t, const uint32_t*, uint32_t, uint64_t,
                 uint32_t, bitoscope_line_change*, uint32_t, uint32_t*);
};

constexpr uint32_t kMostLines = 32;

int changeBatch(bitoscope_decode_input* in, const bitoscope_watched_line* watched, uint32_t watchedCount,
                const uint32_t* sampled, uint32_t sampledCount, uint64_t from, uint32_t previous,
                bitoscope_line_change* out, uint32_t capacity, uint32_t* count) {
  *count = 0;
  if (watchedCount < 1 || watchedCount > kMostLines || sampledCount > kMostLines || capacity == 0) {
    (void)bitoscope_fail_decode(in, "sig.changes: watched or sampled lines are out of range");
    return BITOSCOPE_ERROR;
  }
  try {
    std::array<plugin::WatchedLine, kMostLines> lines{};
    std::array<uint32_t, kMostLines> data{};
    for (uint32_t i = 0; i < watchedCount; ++i) lines[i] = {watched[i].channel, static_cast<plugin::Edge>(watched[i].edge)};
    std::copy_n(sampled, sampledCount, data.begin());
    plugin::detail::CApiWalk env{in};
    (void)plugin::detail::walkChanges<kMostLines, kMostLines, true>(
        env, lines, watchedCount, data, sampledCount, from, previous, [](uint32_t) { return true; },
        [&](const plugin::LineChange<kMostLines, kMostLines>& c) {
          bitoscope_line_change& change = out[(*count)++];
          change = bitoscope_line_change{c.at(), 0, 0, 0, 0};
          for (uint32_t i = 0; i < watchedCount; ++i) {
            change.changed |= (c.changed(i) ? 1u : 0u) << i;
            change.before |= (c.before(i) ? 1u : 0u) << i;
            change.after |= (c.after(i) ? 1u : 0u) << i;
          }
          for (uint32_t m = 0; m < sampledCount; ++m) change.sampled |= (c.sampled(m) ? 1u : 0u) << m;
          return *count < capacity;
        });
    return bitoscope_decode_error(in) != nullptr ? BITOSCOPE_ERROR : BITOSCOPE_OK;
  } catch (...) {
    (void)bitoscope_fail_decode(in, "sig.changes: out of memory");
    return BITOSCOPE_ERROR;
  }
}

const LuaApi kApi = {
    .level = &bitoscope_level,
    .next_edge = &bitoscope_next_edge,
    .next_edge_limit = &bitoscope_next_edge_limit,
    .next_change2 = &bitoscope_next_change2,
    .next_change2_limit = &bitoscope_next_change2_limit,
    .bits = &bitoscope_bits,
    .sample_at = &bitoscope_sample_at,
    .clocked = &bitoscope_clocked,
    .sample_count = &bitoscope_sample_count,
    .release_before = &bitoscope_release_before,
    .decode_error = &bitoscope_decode_error,
    .emit = &bitoscope_emit,
    .put = &bitoscope_put,
    .next_item = &bitoscope_next_item,
    .settle = &bitoscope_settle,
    .settle_due = &bitoscope_settle_due,
    .emit_bytes = &bitoscope_emit_bytes,
    .changes = &changeBatch,
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
