/*
 * SPDX-License-Identifier: MIT
 */

#include "gateway.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <new>
#include <system_error>
#include <utility>

extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

#include "binding.h"
#include "bitoscope_decoder_cpp.h"
#include "c_declaration.h"
#include "decoder_plugin.h"
#include "plugin_version.h"
#include "script_state.h"

namespace bitoscope::lua_gateway {

namespace {

constexpr const char* kGatewayVersion = "0.1.0";

struct Abbreviation {
  std::string upper;
  std::string shown;
  bool alsoWord = false;
};
struct Declaration {
  plugin::DecoderDeclaration decoder;
  std::vector<Abbreviation> abbreviations;
  bool hasCheck = false;
  bool hasLookAhead = false;
};

}

struct Gateway::Script {
  std::string label;
  std::string chunk_name;
  std::string text;
  ScriptState state;
  Declaration decl;
};

namespace {

void pushWhole(lua_State* L, int64_t v) { lua_pushnumber(L, static_cast<lua_Number>(v)); }

void rawField(lua_State* L, int index, const char* key) {
  if (index <= 0 && index > LUA_REGISTRYINDEX) index = lua_gettop(L) + index + 1;
  lua_pushstring(L, key);
  lua_rawget(L, index);
}

std::string stringField(lua_State* L, int index, const char* key) {
  rawField(L, index, key);
  size_t size = 0;
  const char* s = lua_type(L, -1) == LUA_TSTRING ? lua_tolstring(L, -1, &size) : nullptr;
  std::string out = s != nullptr ? std::string(s, size) : std::string();
  lua_pop(L, 1);
  return out;
}

std::optional<int64_t> numberField(lua_State* L, int index, const char* key) {
  rawField(L, index, key);
  std::optional<int64_t> out;
  if (lua_type(L, -1) == LUA_TNUMBER) out = static_cast<int64_t>(lua_tonumber(L, -1));
  lua_pop(L, 1);
  return out;
}

bool boolField(lua_State* L, int index, const char* key) {
  rawField(L, index, key);
  const bool b = lua_toboolean(L, -1) != 0;
  lua_pop(L, 1);
  return b;
}

template <typename F>
void eachOf(lua_State* L, int index, const char* key, F each) {
  rawField(L, index, key);
  if (lua_type(L, -1) == LUA_TTABLE) {
    for (int i = 1;; ++i) {
      lua_rawgeti(L, -1, i);
      if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        break;
      }
      each(L);
      lua_pop(L, 1);
    }
  }
  lua_pop(L, 1);
}

std::vector<std::string> stringList(lua_State* L, int index, const char* key) {
  std::vector<std::string> out;
  eachOf(L, index, key, [&](lua_State* S) {
    size_t size = 0;
    const char* s = lua_tolstring(S, -1, &size);
    if (s != nullptr) out.emplace_back(s, size);
  });
  return out;
}

Declaration readDeclaration(lua_State* L, int index) {
  Declaration out;
  plugin::DecoderDeclaration& d = out.decoder;
  d.name = stringField(L, index, "name");
  d.display = stringField(L, index, "display");
  eachOf(L, index, "abbreviations", [&](lua_State* S) {
    out.abbreviations.push_back({stringField(S, -1, "upper"), stringField(S, -1, "shown"),
                                 boolField(S, -1, "also_word")});
  });
  d.version = stringField(L, index, "version");
  eachOf(L, index, "channels", [&](lua_State* S) {
    d.channels.push_back({.name = stringField(S, -1, "name"),
                          .description = stringField(S, -1, "description"),
                          .optional = boolField(S, -1, "optional"),
                          .display = stringField(S, -1, "display")});
  });
  eachOf(L, index, "options", [&](lua_State* S) {
    d.options.push_back({.name = stringField(S, -1, "name"),
                         .description = stringField(S, -1, "description"),
                         .type = stringField(S, -1, "type") == "integer" ? plugin::OptionType::integer
                                                                       : plugin::OptionType::string,
                         .defaultValue = stringField(S, -1, "default_value"),
                         .allowedValues = stringList(S, -1, "allowed_values"),
                         .min = numberField(S, -1, "min"),
                         .max = numberField(S, -1, "max"),
                         .unsupportedValues = stringList(S, -1, "unsupported_values"),
                         .display = stringField(S, -1, "display")});
  });
  d.unsupportedOptions = stringList(L, index, "unsupported_options");
  d.protocols = stringList(L, index, "protocols");
  eachOf(L, index, "classes", [&](lua_State* S) {
    d.classes.push_back(
        {.id = static_cast<uint32_t>(numberField(S, -1, "id").value_or(0)),
         .name = stringField(S, -1, "name"),
         .format = static_cast<plugin::ValueFormat>(numberField(S, -1, "format").value_or(BITOSCOPE_VALUE_NONE)),
         .hexDigits = static_cast<uint32_t>(numberField(S, -1, "hex_digits").value_or(0)),
         .hexDigitsOption = stringField(S, -1, "hex_digits_option"),
         .role = static_cast<plugin::AnnotationRole>(numberField(S, -1, "role").value_or(BITOSCOPE_ROLE_NONE))});
  });
  eachOf(L, index, "rows", [&](lua_State* S) {
    d.rows.push_back({static_cast<uint32_t>(numberField(S, -1, "id").value_or(0)), stringField(S, -1, "name")});
  });
  eachOf(L, index, "row_sources", [&](lua_State* S) {
    plugin::RowSource s;
    s.row = static_cast<uint32_t>(numberField(S, -1, "row").value_or(0));
    s.cls = static_cast<uint32_t>(numberField(S, -1, "cls").value_or(0));
    s.sourceRow = static_cast<uint32_t>(numberField(S, -1, "source_row").value_or(0));
    eachOf(S, -1, "source_classes",
           [&](lua_State* T) { s.sourceClasses.push_back(static_cast<uint32_t>(lua_tonumber(T, -1))); });
    s.enableOption = stringField(S, -1, "enable_option");
    s.rule = static_cast<plugin::RowRule>(numberField(S, -1, "rule").value_or(0));
    rawField(S, -1, "grid");
    s.grid = {stringField(S, -1, "rate_option"), static_cast<uint32_t>(numberField(S, -1, "offset_bits").value_or(0)),
              stringField(S, -1, "count_option"), stringField(S, -1, "order_option")};
    lua_pop(S, 1);
    rawField(S, -1, "edges");
    s.edges = {stringField(S, -1, "clock_channel"), stringField(S, -1, "data_channel"),
               stringField(S, -1, "count_option"), static_cast<uint32_t>(numberField(S, -1, "count").value_or(0))};
    lua_pop(S, 1);
    d.rowSources.push_back(std::move(s));
  });
  eachOf(L, index, "outputs", [&](lua_State* S) {
    d.outputs.push_back({.name = stringField(S, -1, "name"),
                         .description = stringField(S, -1, "description"),
                         .type = static_cast<plugin::StreamType>(numberField(S, -1, "type").value_or(0)),
                         .width = static_cast<uint32_t>(numberField(S, -1, "width").value_or(0)),
                         .widthOption = stringField(S, -1, "width_option"),
                         .requiresChannel = stringField(S, -1, "requires_channel"),
                         .display = stringField(S, -1, "display"),
                         .carries = stringField(S, -1, "carries"),
                         .carriesInput = stringField(S, -1, "carries_input")});
  });
  eachOf(L, index, "inputs", [&](lua_State* S) {
    d.inputs.push_back({.name = stringField(S, -1, "name"),
                        .description = stringField(S, -1, "description"),
                        .type = static_cast<plugin::StreamType>(numberField(S, -1, "type").value_or(0)),
                        .width = static_cast<uint32_t>(numberField(S, -1, "width").value_or(0)),
                        .optional = boolField(S, -1, "optional"),
                        .display = stringField(S, -1, "display")});
  });
  d.allowsSharedChannels = boolField(L, index, "allows_shared_channels");
  rawField(L, index, "check");
  out.hasCheck = lua_type(L, -1) == LUA_TFUNCTION;
  lua_pop(L, 1);
  rawField(L, index, "look_ahead_samples");
  out.hasLookAhead = lua_type(L, -1) == LUA_TFUNCTION;
  lua_pop(L, 1);
  return out;
}

int refusedDecode(void*, const bitoscope_decode_job*, bitoscope_decode_input*, bitoscope_annotations*) {
  return BITOSCOPE_ERROR;
}

void pushDeclaration(const ScriptState& state) {
  lua_State* L = state.get();
  state.pushRecord();
  rawField(L, -1, "decl");
  lua_remove(L, -2);
}

bool integerOption(const Declaration& d, const char* name) {
  for (const plugin::DecoderOption& o : d.decoder.options) {
    if (o.name == name) return o.type == plugin::OptionType::integer;
  }
  return false;
}

void pushFacts(lua_State* L, const bitoscope_decode_job* job, const Declaration& d) {
  lua_createtable(L, 0, 7);
  lua_createtable(L, 0, static_cast<int>(job->num_channels));
  for (size_t i = 0; i < job->num_channels; ++i) {
    pushWhole(L, job->channels[i].input_channel);
    lua_setfield(L, -2, job->channels[i].name);
  }
  lua_setfield(L, -2, "channels");
  lua_createtable(L, 0, static_cast<int>(job->num_options));
  for (size_t i = 0; i < job->num_options; ++i) {
    const bitoscope_job_option& o = job->options[i];
    int64_t v = 0;
    const char* end = o.value + std::strlen(o.value);
    if (integerOption(d, o.name) && std::from_chars(o.value, end, v).ec == std::errc()) {
      lua_pushnumber(L, static_cast<lua_Number>(v));
    } else {
      lua_pushstring(L, o.value);
    }
    lua_setfield(L, -2, o.name);
  }
  lua_setfield(L, -2, "options");
  pushWhole(L, static_cast<int64_t>(job->sample_rate_milli_hz));
  lua_setfield(L, -2, "sample_rate_millihertz");
  pushWhole(L, job->capture_channels);
  lua_setfield(L, -2, "capture_channels");
  pushWhole(L, static_cast<int64_t>(job->total_samples));
  lua_setfield(L, -2, "total_samples");
  lua_pushboolean(L, job->total_is_bound != 0);
  lua_setfield(L, -2, "total_is_bound");
  lua_pushstring(L, job->decoder);
  lua_setfield(L, -2, "decoder");
}

void pushWhere(lua_State* L) {
  lua_Debug ar;
  lua_pushvalue(L, -1);
  if (lua_getinfo(L, ">S", &ar) != 0) {
    lua_pushfstring(L, "%s:%d:", ar.short_src, ar.linedefined);
  } else {
    lua_pushliteral(L, "?:");
  }
}

// WHY: Calls into a script run in protected mode: no C++ object with a destructor may live in them,
// a Lua error can longjmp.

int callCheck(lua_State* L) {
  const auto* job = static_cast<const bitoscope_decode_job*>(lua_touserdata(L, 1));
  const auto* script = static_cast<const Gateway::Script*>(lua_touserdata(L, 2));
  pushDeclaration(script->state);
  rawField(L, -1, "check");
  pushFacts(L, job, script->decl);
  lua_pushvalue(L, -2);
  lua_insert(L, -2);
  lua_call(L, 1, 1);
  const int type = lua_type(L, -1);
  if (type == LUA_TNIL || type == LUA_TSTRING) return 1;
  lua_pushvalue(L, -2);
  pushWhere(L);
  return luaL_error(L,
                    "%s check returned %s; it returns nothing to accept job, or string saying why "
                    "not",
                    lua_tostring(L, -1), luaL_typename(L, -3));
}

int callLookAhead(lua_State* L) {
  const auto* job = static_cast<const bitoscope_decode_job*>(lua_touserdata(L, 1));
  const auto* script = static_cast<const Gateway::Script*>(lua_touserdata(L, 2));
  pushDeclaration(script->state);
  rawField(L, -1, "look_ahead_samples");
  lua_pushvalue(L, 4);
  pushFacts(L, job, script->decl);
  lua_call(L, 1, 1);
  const int type = lua_type(L, 5);
  if (type == LUA_TNIL) {
    lua_pushinteger(L, 0);
    return 1;
  }
  char returned[64];
  if (type == LUA_TNUMBER) {
    const double v = static_cast<double>(lua_tonumber(L, 5));
    if (v >= 0 && v <= static_cast<double>(kMaxLuaSamples) && v == std::floor(v)) {
      lua_pushnumber(L, v);
      return 1;
    }
    if (v != v) {
      std::snprintf(returned, sizeof returned, "nan");
    } else if (std::isinf(v)) {
      std::snprintf(returned, sizeof returned, "%s", v > 0 ? "inf" : "-inf");
    } else {
      std::snprintf(returned, sizeof returned, "%.17g", v);
    }
  } else {
    std::snprintf(returned, sizeof returned, "%s", luaL_typename(L, 5));
  }
  lua_pushvalue(L, 4);
  pushWhere(L);
  return luaL_error(L,
                    "%s look_ahead_samples returned %s; it returns nothing, or whole number of samples from 0 to "
                    "2^53 - 1 (most it needs in its window at once)",
                    lua_tostring(L, -1), returned);
}

void pushStreams(lua_State* L, const bitoscope_decode_job* job, bool outputs) {
  lua_newtable(L);
  const size_t n = outputs ? job->num_outputs : job->num_inputs;
  for (size_t i = 0; i < n; ++i) {
    const char* name = outputs ? job->outputs[i].name : job->inputs[i].name;
    uint32_t index = 0;
    const int answer = outputs ? bitoscope_output_wanted(job, name, &index) : bitoscope_input_bound(job, name, &index);
    if (answer != BITOSCOPE_OK) continue;
    pushWhole(L, index);
    lua_setfield(L, -2, name);
  }
}

int callDecode(lua_State* L) {
  const auto* job = static_cast<const bitoscope_decode_job*>(lua_touserdata(L, 1));
  void* in = lua_touserdata(L, 2);
  void* out = lua_touserdata(L, 3);
  const auto* script = static_cast<const Gateway::Script*>(lua_touserdata(L, 4));
  lua_settop(L, 0);
  lua_pushvalue(L, lua_upvalueindex(1));
  rawField(L, -1, "decl");
  if (!lua_istable(L, -1)) {
    return luaL_error(L, "%s declared no decoder when it was loaded for job", script->chunk_name.c_str());
  }
  rawField(L, -1, "decode");
  lua_pushcfunction(L, &binding::pushDecodeArguments);
  lua_pushlightuserdata(L, in);
  lua_pushlightuserdata(L, out);
  pushFacts(L, job, script->decl);
  pushStreams(L, job, true);
  pushStreams(L, job, false);
  lua_call(L, 5, 3);
  lua_pushvalue(L, 3);
  lua_insert(L, 4);
  lua_call(L, 3, 1);
  if (lua_isnil(L, -1)) return 0;
  lua_pushvalue(L, 3);
  pushWhere(L);
  return luaL_error(L, "%s decode returned %s; it returns nothing (it calls error() to fail job)",
                    lua_tostring(L, -1), luaL_typename(L, 4));
}

std::string popMessage(lua_State* L) {
  const char* m = lua_tostring(L, -1);
  std::string message = m != nullptr ? m : std::string("(error is ") + luaL_typename(L, -1) + " value)";
  lua_pop(L, 1);
  return message;
}

std::optional<std::string> checkJob(const bitoscope_decode_job& job, const Declaration& d) {
  if (job.total_samples > kMaxLuaSamples) {
    return (job.total_is_bound != 0 ? "capture may hold up to " : "capture has ") + std::to_string(job.total_samples) +
           " samples; Lua decoder takes at most " + std::to_string(kMaxLuaSamples) +
           " (2^53 - 1), most Lua number holds exactly";
  }
  if (job.sample_rate_milli_hz > kMaxLuaSamples) {
    return "sample rate of " + std::to_string(job.sample_rate_milli_hz) +
           " mHz is more than Lua number holds exactly (2^53 - 1)";
  }
  for (size_t i = 0; i < job.num_options; ++i) {
    const char* name = job.options[i].name;
    if (!integerOption(d, name)) continue;
    int64_t v = 0;
    const char* text = job.options[i].value;
    const char* end = text + std::strlen(text);
    const auto parsed = std::from_chars(text, end, v);
    if (parsed.ec != std::errc() || parsed.ptr != end) {
      return std::string("option '") + name + "' is " + text + ", not whole number from -2^63 to 2^63 - 1";
    }
    if (v > static_cast<int64_t>(kMaxLuaSamples) || v < -static_cast<int64_t>(kMaxLuaSamples)) {
      return std::string("option '") + name + "' is " + text +
             ", which Lua decoder cannot hold exactly: use value from -(2^53 - 1) to 2^53 - 1";
    }
  }
  return std::nullopt;
}

int checkFn(void* user, const bitoscope_decode_job* job, bitoscope_reply* reply) {
  auto* script = static_cast<Gateway::Script*>(user);
  try {
    if (auto refused = checkJob(*job, script->decl)) {
      bitoscope_reply_message(reply, refused->c_str());
      return BITOSCOPE_ERROR;
    }
    lua_State* L = script->state.get();
    const int top = lua_gettop(L);
    if (script->decl.hasCheck) {
      lua_pushcfunction(L, &callCheck);
      lua_pushlightuserdata(L, const_cast<bitoscope_decode_job*>(job));
      lua_pushlightuserdata(L, script);
      const int status = lua_pcall(L, 2, 1, 0);
      if (status != LUA_OK || !lua_isnil(L, -1)) {
        const std::string message = popMessage(L);
        lua_settop(L, top);
        bitoscope_reply_message(reply, message.c_str());
        return BITOSCOPE_ERROR;
      }
      lua_settop(L, top);
    }
    if (script->decl.hasLookAhead) {
      lua_pushcfunction(L, &callLookAhead);
      lua_pushlightuserdata(L, const_cast<bitoscope_decode_job*>(job));
      lua_pushlightuserdata(L, script);
      if (lua_pcall(L, 2, 1, 0) != LUA_OK) {
        const std::string message = popMessage(L);
        lua_settop(L, top);
        bitoscope_reply_message(reply, message.c_str());
        return BITOSCOPE_ERROR;
      }
      const auto samples = static_cast<uint64_t>(lua_tonumber(L, -1));
      lua_settop(L, top);
      if (samples != 0) bitoscope_reply_look_ahead_samples(reply, samples);
    }
    return BITOSCOPE_OK;
  } catch (const std::bad_alloc&) {
    bitoscope_reply_message(reply, "Lua gateway ran out of memory checking job");
    return BITOSCOPE_ERROR;
  }
}

int decodeFn(void* user, const bitoscope_decode_job* job, bitoscope_decode_input* in, bitoscope_annotations* out) {
  auto* script = static_cast<Gateway::Script*>(user);
  try {
    auto state = ScriptState::create();
    if (!state) {
      bitoscope_fail_decode(in, state.error().c_str());
      return BITOSCOPE_ERROR;
    }
    if (auto ran = state->run(script->chunk_name, script->text); !ran) {
      bitoscope_fail_decode(in, (script->label + " did not load again for job: " + ran.error()).c_str());
      return BITOSCOPE_ERROR;
    }
    lua_State* L = state->get();
    state->pushRecord();
    lua_pushcclosure(L, &callDecode, 1);
    lua_pushlightuserdata(L, const_cast<bitoscope_decode_job*>(job));
    lua_pushlightuserdata(L, in);
    lua_pushlightuserdata(L, out);
    lua_pushlightuserdata(L, script);
    if (lua_pcall(L, 4, 0, 0) != LUA_OK) {
      const std::string message = popMessage(L);
      if (bitoscope_decode_error(in) == nullptr) bitoscope_fail_decode(in, message.c_str());
      return BITOSCOPE_ERROR;
    }
    return bitoscope_decode_error(in) == nullptr ? BITOSCOPE_OK : BITOSCOPE_ERROR;
  } catch (const std::bad_alloc&) {
    bitoscope_fail_decode(in, "Lua gateway ran out of memory");
    return BITOSCOPE_ERROR;
  }
}

struct Source {
  std::string label;
  std::string chunk_name;
  std::string text;
  std::string unreadable;
};

std::expected<std::unique_ptr<Gateway::Script>, std::string> loadScript(Source source) {
  if (!source.unreadable.empty()) return std::unexpected(source.unreadable);
  auto state = ScriptState::create();
  if (!state) return std::unexpected(state.error());
  if (auto ran = state->run(source.chunk_name, source.text); !ran) return std::unexpected(ran.error());
  lua_State* L = state->get();
  pushDeclaration(*state);
  if (lua_isnil(L, -1)) {
    lua_pop(L, 1);
    return std::unexpected(source.chunk_name + ": it declares no decoder (script calls decoder { ... } once)");
  }
  Declaration decl = readDeclaration(L, -1);
  lua_pop(L, 1);
  const plugin::DecoderDeclaration& d = decl.decoder;
  if (d.version.empty()) {
    return std::unexpected(source.chunk_name + ": decoder '" + d.name +
                           "' declares no version (script writes version = \"X.Y.Z\", such as \"1.0.0\")");
  }
  if (auto parsed = ipc::parsePluginVersion(d.version); !parsed) {
    return std::unexpected(source.chunk_name + ": decoder '" + d.name + "': " + parsed.error());
  }
  return std::unique_ptr<Gateway::Script>(new Gateway::Script{std::move(source.label), std::move(source.chunk_name),
                                                              std::move(source.text), std::move(*state),
                                                              std::move(decl)});
}

}

int refuseDeclaration(lua_State* L) {
  std::string refusal;
  try {
    plugin::DecoderDeclaration d = std::move(readDeclaration(L, 1).decoder);
    // WHY: Stand-in version: loadScript refuses script's version afterwards in gateway's own words.
    d.version = "1.0.0";
    const plugin_c_sdk::CDeclaration decoder(std::move(d), &refusedDecode, nullptr, nullptr);
    auto checked = plugin_c_sdk::declarationOf(decoder.get(), sizeof(bitoscope_decoder));
    if (!checked) refusal = std::move(checked.error());
  } catch (const std::bad_alloc&) {
    refusal = "Lua gateway ran out of memory checking declaration";
  }
  if (refusal.empty()) return 0;
  lua_pushlstring(L, refusal.data(), refusal.size());
  return 1;
}

Gateway::Gateway() = default;
Gateway::~Gateway() = default;

std::expected<std::unique_ptr<Gateway>, std::string> Gateway::load(
    std::span<const EmbeddedScript> builtins, const std::optional<std::filesystem::path>& script_dir) {
  try {
    std::vector<Source> sources;
    for (const EmbeddedScript& s : builtins) {
      sources.push_back({std::string("built-in script ") + s.name, s.name, std::string(s.text, s.size), ""});
    }
    if (script_dir) {
      std::error_code ec;
      if (!std::filesystem::is_directory(*script_dir, ec)) {
        return std::unexpected("script directory '" + script_dir->string() + "' is not directory");
      }
      std::vector<std::filesystem::path> files;
      for (std::filesystem::directory_iterator it(*script_dir, ec), end; !ec && it != end; it.increment(ec)) {
        const std::filesystem::path& p = it->path();
        std::error_code type_ec;
        if (p.extension() == ".lua" && it->is_regular_file(type_ec)) files.push_back(p);
      }
      if (ec) {
        return std::unexpected("script directory '" + script_dir->string() + "' could not be read: " +
                               ec.message());
      }
      std::sort(files.begin(), files.end(),
                [](const auto& a, const auto& b) { return a.filename().u8string() < b.filename().u8string(); });
      for (const auto& p : files) {
        const std::string name = p.filename().string();
        std::ifstream in(p, std::ios::binary);
        std::string text;
        if (in) text.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        const bool read = in.good() || in.eof();
        sources.push_back({"script " + p.string(), name, std::move(text), read ? "" : "file could not be read"});
      }
    }

    std::unique_ptr<Gateway> gateway(new Gateway());
    std::map<std::string, std::string> taken;
    for (Source& source : sources) {
      const std::string label = source.label;
      auto script = loadScript(std::move(source));
      std::string message;
      if (!script) {
        message = label + " is left out: " + script.error();
      } else if (auto it = taken.find((*script)->decl.decoder.name); it != taken.end()) {
        message = label + " is left out: its decoder '" + (*script)->decl.decoder.name +
                  "' has name already taken by decoder of " + it->second;
      } else {
        taken.emplace((*script)->decl.decoder.name, label);
        gateway->scripts_.push_back(std::move(*script));
        continue;
      }
      bitoscope_log(BITOSCOPE_LOG_WARNING, "%s", message.c_str());
      gateway->messages_.push_back(std::move(message));
    }
    return gateway;
  } catch (const std::bad_alloc&) {
    return std::unexpected("Lua gateway ran out of memory loading its scripts");
  }
}

const std::vector<std::string>& Gateway::messages() const { return messages_; }

std::vector<std::string> Gateway::decoderNames() const {
  std::vector<std::string> names;
  for (const auto& s : scripts_) names.push_back(s->decl.decoder.name);
  return names;
}

int Gateway::list(void* user, bitoscope_decoders* decoders) {
  auto* gateway = static_cast<Gateway*>(user);
  try {
    if (bitoscope_set_decoder_plugin_version(decoders, kGatewayVersion) != BITOSCOPE_OK ||
        bitoscope_set_decoder_plugin_runs_scripts(decoders, 1) != BITOSCOPE_OK) {
      return BITOSCOPE_ERROR;
    }
    for (const auto& script : gateway->scripts_) {
      for (const Abbreviation& a : script->decl.abbreviations) {
        (void)bitoscope_add_abbreviation(a.upper.c_str(), a.shown.empty() ? nullptr : a.shown.c_str(),
                                         a.alsoWord ? 1 : 0);
      }
      const plugin_c_sdk::CDeclaration decoder(script->decl.decoder, &decodeFn, &checkFn, script.get());
      if (bitoscope_add_decoder(decoders, decoder.get()) != BITOSCOPE_OK) {
        bitoscope_log(BITOSCOPE_LOG_WARNING, "%s is left out: SDK refused its decoder '%s' (reason is above)",
                      script->label.c_str(), script->decl.decoder.name.c_str());
      }
    }
    return BITOSCOPE_OK;
  } catch (const std::bad_alloc&) {
    bitoscope_log(BITOSCOPE_LOG_ERROR, "Lua gateway ran out of memory listing its decoders");
    return BITOSCOPE_ERROR;
  }
}

std::expected<CommandLine, std::string> parseCommandLine(std::span<const std::string> args) {
  CommandLine out;
  for (size_t i = 0; i < args.size(); ++i) {
    if (i == 0 || args[i] != "--script-dir") {
      out.plugin_args.push_back(args[i]);
      continue;
    }
    if (i + 1 >= args.size()) return std::unexpected(std::string("--script-dir needs directory"));
    if (out.script_dir) return std::unexpected(std::string("--script-dir is given twice"));
    std::filesystem::path dir(args[++i]);
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec)) {
      return std::unexpected("--script-dir '" + dir.string() + "' is not directory");
    }
    out.script_dir = std::move(dir);
  }
  return out;
}

int runGateway(int argc, char** argv, std::span<const EmbeddedScript> builtins) {
  try {
    const std::vector<std::string> args(argv, argv + argc);
    const std::string program = args.empty() ? std::string("decoders_luajit") : args[0];
    auto command_line = parseCommandLine(args);
    if (!command_line) {
      std::fprintf(stderr, "%s: %s\n", program.c_str(), command_line.error().c_str());
      return 2;
    }
    auto gateway = Gateway::load(builtins, command_line->script_dir);
    if (!gateway) {
      std::fprintf(stderr, "%s: %s\n", program.c_str(), gateway.error().c_str());
      return 1;
    }
    std::vector<char*> plugin_argv;
    for (auto& a : command_line->plugin_args) plugin_argv.push_back(a.data());
    plugin_argv.push_back(nullptr);
    return bitoscope_run_decoder_plugin(static_cast<int>(command_line->plugin_args.size()), plugin_argv.data(),
                                        &Gateway::list, gateway->get());
  } catch (const std::bad_alloc&) {
    std::fputs("Lua gateway ran out of memory\n", stderr);
    return 1;
  }
}

}
