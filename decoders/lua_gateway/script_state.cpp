/*
 * SPDX-License-Identifier: MIT
 */

#include "script_state.h"

#include <cstring>

extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

#include "binding.h"
#include "embedded_script.h"
#include "gateway.h"

namespace bitoscope::lua_gateway {

namespace {

std::string popError(lua_State* L) {
  const char* m = lua_tostring(L, -1);
  std::string message = m != nullptr ? m : std::string("(error is ") + luaL_typename(L, -1) + " value)";
  lua_pop(L, 1);
  return message;
}

int setUp(lua_State* L) {
  luaL_openlibs(L);
  binding::prepare(L);
  const EmbeddedScript* host = nullptr;
  for (const EmbeddedScript& s : hostScripts()) {
    if (std::strcmp(s.name, "gateway_host.lua") == 0) host = &s;
  }
  if (host == nullptr) return luaL_error(L, "gateway was built without gateway_host.lua");
  if (luaL_loadbufferx(L, host->text, host->size, "@gateway_host.lua", "t") != LUA_OK) return lua_error(L);
  lua_call(L, 0, 1);
  lua_getfield(L, -1, "new_env");
  lua_pushcfunction(L, &refuseDeclaration);
  lua_call(L, 1, 2);
  return 2;
}

}

std::expected<ScriptState, std::string> ScriptState::create() {
  lua_State* L = luaL_newstate();
  if (L == nullptr) return std::unexpected("Lua state could not be made: out of memory");
  ScriptState state(L);
  lua_pushcfunction(L, &setUp);
  if (lua_pcall(L, 0, 2, 0) != LUA_OK) return std::unexpected("Lua state could not be set up: " + popError(L));
  state.record_ref_ = luaL_ref(L, LUA_REGISTRYINDEX);
  state.env_ref_ = luaL_ref(L, LUA_REGISTRYINDEX);
  if (state.record_ref_ == LUA_REFNIL || state.env_ref_ == LUA_REFNIL) {
    return std::unexpected("Lua state could not be set up: gateway's host code made no environment");
  }
  return state;
}

ScriptState::~ScriptState() {
  if (L_ != nullptr) lua_close(L_);
}

ScriptState::ScriptState(ScriptState&& other) noexcept
    : L_(other.L_), env_ref_(other.env_ref_), record_ref_(other.record_ref_) {
  other.L_ = nullptr;
}

ScriptState& ScriptState::operator=(ScriptState&& other) noexcept {
  if (this != &other) {
    if (L_ != nullptr) lua_close(L_);
    L_ = other.L_;
    env_ref_ = other.env_ref_;
    record_ref_ = other.record_ref_;
    other.L_ = nullptr;
  }
  return *this;
}

namespace {

std::expected<void, std::string> loadAndCall(lua_State* L, int env_ref, std::string_view chunk_name,
                                             std::string_view text, int results) {
  const std::string name = "@" + std::string(chunk_name);
  if (luaL_loadbufferx(L, text.data(), text.size(), name.c_str(), "t") != LUA_OK) {
    return std::unexpected(popError(L));
  }
  lua_rawgeti(L, LUA_REGISTRYINDEX, env_ref);
  lua_setfenv(L, -2);
  if (lua_pcall(L, 0, results, 0) != LUA_OK) return std::unexpected(popError(L));
  return {};
}

}

std::expected<void, std::string> ScriptState::run(std::string_view chunk_name, std::string_view text) {
  const int top = lua_gettop(L_);
  auto result = loadAndCall(L_, env_ref_, chunk_name, text, 0);
  lua_settop(L_, top);
  return result;
}

std::expected<std::vector<std::string>, std::string> ScriptState::evaluate(std::string_view chunk_name,
                                                                          std::string_view text) {
  const int top = lua_gettop(L_);
  if (auto ran = loadAndCall(L_, env_ref_, chunk_name, text, LUA_MULTRET); !ran) {
    lua_settop(L_, top);
    return std::unexpected(ran.error());
  }
  std::vector<std::string> out;
  for (int i = top + 1; i <= lua_gettop(L_); ++i) {
    const int type = lua_type(L_, i);
    if (type == LUA_TSTRING || type == LUA_TNUMBER) {
      // WHY: Number is converted in a copy: lua_tolstring converts the stack value in place and would change the number.
      lua_pushvalue(L_, i);
      size_t size = 0;
      const char* s = lua_tolstring(L_, -1, &size);
      out.emplace_back(s, size);
      lua_pop(L_, 1);
    } else {
      out.emplace_back(std::string("(") + lua_typename(L_, type) + ")");
    }
  }
  lua_settop(L_, top);
  return out;
}

void ScriptState::pushRecord() const { lua_rawgeti(L_, LUA_REGISTRYINDEX, record_ref_); }

}
