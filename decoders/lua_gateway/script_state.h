/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <expected>
#include <string>
#include <string_view>
#include <vector>

struct lua_State;

namespace bitoscope::lua_gateway {

class ScriptState {
 public:
  static std::expected<ScriptState, std::string> create();

  ~ScriptState();
  ScriptState(ScriptState&& other) noexcept;
  ScriptState& operator=(ScriptState&& other) noexcept;
  ScriptState(const ScriptState&) = delete;
  ScriptState& operator=(const ScriptState&) = delete;

  lua_State* get() const { return L_; }

  std::expected<void, std::string> run(std::string_view chunk_name, std::string_view text);

  std::expected<std::vector<std::string>, std::string> evaluate(std::string_view chunk_name, std::string_view text);

  void pushRecord() const;

 private:
  explicit ScriptState(lua_State* L) : L_(L) {}
  lua_State* L_ = nullptr;
  int env_ref_ = 0;
  int record_ref_ = 0;
};

}
