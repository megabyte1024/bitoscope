-- SPDX-License-Identifier: MIT

local type, pairs, ipairs, tostring, error, pcall, select = type, pairs, ipairs, tostring, error, pcall, select
local rawget, getmetatable = rawget, getmetatable
local floor, format = math.floor, string.format
local real_collectgarbage = collectgarbage

local LIMIT = 9007199254740992
local U32 = 4294967296

local H = {}

local GLOBALS = {
  "assert", "error", "getmetatable", "ipairs", "next", "pairs", "pcall", "print", "rawequal", "rawget",
  "rawset", "select", "setmetatable", "tonumber", "tostring", "type", "xpcall", "_VERSION",
}
local STRING = { "byte", "char", "find", "format", "gmatch", "gsub", "len", "lower", "match", "rep",
                 "reverse", "sub", "upper" }
local TABLE = { "concat", "insert", "remove", "sort" }
local MATH = { "abs", "acos", "asin", "atan", "ceil", "cos", "deg", "exp", "floor", "fmod", "huge", "log",
               "max", "min", "modf", "pi", "rad", "random", "randomseed", "sin", "sqrt", "tan" }
local BIT = { "tobit", "tohex", "bnot", "band", "bor", "bxor", "lshift", "rshift", "arshift", "rol", "ror",
              "bswap" }

local function pick(from, names)
  local t = {}
  for _, k in ipairs(names) do t[k] = from[k] end
  return t
end

local function limited_collectgarbage(opt, ...)
  if select("#", ...) == 0 and (opt == nil or opt == "collect" or opt == "count") then
    return real_collectgarbage(opt)
  end
  error("collectgarbage: only collectgarbage(), collectgarbage(\"collect\") and collectgarbage(\"count\") "
        .. "are allowed in decoder script", 2)
end

local function fail(where, msg)
  error(where .. ": " .. msg, 0)
end

local function whole(x, low, high)
  return type(x) == "number" and x == floor(x) and x >= low and x < high
end

local function keys(t, allowed, where)
  for k in pairs(t) do
    if type(k) ~= "string" or not allowed[k] then
      fail(where, "unknown field '" .. tostring(k) .. "'")
    end
  end
end

local function set(list)
  local s = {}
  for _, k in ipairs(list) do s[k] = true end
  return s
end

local function text(t, key, where, required)
  local v = rawget(t, key)
  if v == nil then
    if required then fail(where, "'" .. key .. "' is missing") end
    return nil
  end
  if type(v) ~= "string" then fail(where, "'" .. key .. "' must be string, not " .. type(v)) end
  if required and v == "" then fail(where, "'" .. key .. "' is empty") end
  return v
end

local function flag(t, key, where)
  local v = rawget(t, key)
  if v == nil then return false end
  if type(v) ~= "boolean" then fail(where, "'" .. key .. "' must be true or false, not " .. type(v)) end
  return v
end

local function list(t, key, where, required)
  local v = rawget(t, key)
  if v == nil then
    if required then fail(where, "'" .. key .. "' is missing") end
    return {}
  end
  if type(v) ~= "table" then fail(where, "'" .. key .. "' must be list, not " .. type(v)) end
  local n, count = #v, 0
  for k in pairs(v) do
    if not whole(k, 1, n + 1) then
      fail(where, "'" .. key .. "' must be list (it has key " .. tostring(k) .. ")")
    end
    count = count + 1
  end
  if count ~= n then fail(where, "'" .. key .. "' must be list") end
  if required and n == 0 then fail(where, "'" .. key .. "' is empty") end
  return v
end

local function entry(v, where)
  if type(v) ~= "table" then fail(where, "must be table, not " .. type(v)) end
  return v
end

local function id(t, key, where)
  local v = rawget(t, key)
  if v == nil then fail(where, "'" .. key .. "' is missing") end
  if not whole(v, 0, U32) then fail(where, "'" .. key .. "' must be whole number from 0 to 2^32 - 1") end
  return v
end

local function integer_text(v, where, what)
  if not whole(v, -LIMIT + 1, LIMIT) then
    fail(where, what .. " must be whole number (from -(2^53 - 1) to 2^53 - 1), not " .. tostring(v))
  end
  return format("%d", v)
end

local FORMATS = { none = 0, hex = 1, address7 = 2, n_of_m = 3, decimal = 4, n_of_m_bytes = 5, bytes = 6 }
local ROLES = { none = 0, data = 1, start = 2, stop = 3, ok = 4, error = 5, control = 6, address = 7, bit = 8,
                info = 9 }

local STREAM_TYPES = { words = 1, ["spi-transfers"] = 2 }

local function stream_type(t, where)
  local name = text(t, "type", where, true)
  local v = STREAM_TYPES[name]
  if v == nil then fail(where, "'type' must be \"words\" or \"spi-transfers\", not \"" .. name .. "\"") end
  return v
end

local function width_of(t, where)
  local v = rawget(t, "width")
  if v == nil then return 0 end
  if not whole(v, 0, U32) then fail(where, "'width' must be whole number from 0 to 2^32 - 1") end
  return v
end

local function outputs_of(decl)
  local out = {}
  for i, v in ipairs(list(decl, "outputs", "declaration", false)) do
    local where = "outputs[" .. i .. "]"
    local o = entry(v, where)
    keys(o, set({ "name", "description", "type", "width", "width_option", "channel", "display", "carries",
                  "carries_input" }), where)
    local r = { name = text(o, "name", where, true), description = text(o, "description", where, false) or "",
                display = text(o, "display", where, false) or "",
                type = stream_type(o, where), width = width_of(o, where),
                width_option = text(o, "width_option", where, false), channel = text(o, "channel", where, false),
                carries = text(o, "carries", where, false),
                carries_input = text(o, "carries_input", where, false) }
    if rawget(o, "width") ~= nil and r.width_option ~= nil then
      fail(where, "'width' and 'width_option' are both given; output takes one")
    end
    out[i] = r
  end
  return out
end

local function inputs_of(decl)
  local out = {}
  for i, v in ipairs(list(decl, "inputs", "declaration", false)) do
    local where = "inputs[" .. i .. "]"
    local o = entry(v, where)
    keys(o, set({ "name", "description", "type", "width", "required", "display" }), where)
    out[i] = { name = text(o, "name", where, true), description = text(o, "description", where, false) or "",
               display = text(o, "display", where, false) or "",
               type = stream_type(o, where), width = width_of(o, where), required = flag(o, "required", where) }
  end
  return out
end

local ROW_RULE_WORD_LIST, ROW_RULE_BIT_GRID, ROW_RULE_CLOCK_EDGES = 1, 2, 3

local function whole_or_zero(t, key, where)
  local v = rawget(t, key)
  if v == nil then return 0 end
  if not whole(v, 0, U32) then fail(where, "'" .. key .. "' must be whole number from 0 to 2^32 - 1") end
  return v
end

local function row_source_of(row_id, v, where)
  local s = entry(v, where)
  keys(s, set({ "row", "classes", "class", "enable", "grid", "edges", "word_list" }), where)
  local r = { row = row_id, source_row = id(s, "row", where), source_classes = {}, rate_option = "", offset_bits = 0,
              count_option = "", order_option = "", clock = "", data = "", count = 0 }
  if rawget(s, "classes") == nil then fail(where, "'classes' is missing") end
  for j, c in ipairs(list(s, "classes", where, false)) do
    if not whole(c, 0, U32) then fail(where, "classes[" .. j .. "] must be whole number from 0 to 2^32 - 1") end
    r.source_classes[j] = c
  end
  r.cls = id(s, "class", where)
  r.enable = text(s, "enable", where, false) or ""
  local given = 0
  for _, k in ipairs({ "grid", "edges", "word_list" }) do
    if rawget(s, k) ~= nil then given = given + 1 end
  end
  if given == 0 then fail(where, "needs one of 'grid', 'edges' and 'word_list'") end
  if given > 1 then fail(where, "has more than one of 'grid', 'edges' and 'word_list'") end
  if rawget(s, "word_list") ~= nil then
    if rawget(s, "word_list") ~= true then fail(where, "'word_list' must be true") end
    r.rule = ROW_RULE_WORD_LIST
  elseif rawget(s, "grid") ~= nil then
    local at = where .. ".grid"
    local g = entry(rawget(s, "grid"), at)
    keys(g, set({ "rate_option", "offset_bits", "count_option", "order_option" }), at)
    r.rule = ROW_RULE_BIT_GRID
    r.rate_option = text(g, "rate_option", at, true)
    r.offset_bits = whole_or_zero(g, "offset_bits", at)
    r.count_option = text(g, "count_option", at, true)
    r.order_option = text(g, "order_option", at, true)
  else
    local at = where .. ".edges"
    local e = entry(rawget(s, "edges"), at)
    keys(e, set({ "clock", "data", "count_option", "count" }), at)
    r.rule = ROW_RULE_CLOCK_EDGES
    r.clock = text(e, "clock", at, true)
    r.data = text(e, "data", at, true)
    if rawget(e, "count") ~= nil and rawget(e, "count_option") ~= nil then
      fail(at, "'count' and 'count_option' are both given; edges take one")
    end
    r.count_option = text(e, "count_option", at, false) or ""
    r.count = whole_or_zero(e, "count", at)
  end
  return r
end

local function options_of(decl)
  local out = {}
  for i, v in ipairs(list(decl, "options", "options", false)) do
    local where = "options[" .. i .. "]"
    local o = entry(v, where)
    keys(o, set({ "name", "description", "type", "default", "values", "min", "max", "unsupported_values", "display" }),
         where)
    local name = text(o, "name", where, true)
    where = "option '" .. name .. "'"
    local kind = text(o, "type", where, true)
    if kind ~= "string" and kind ~= "integer" then
      fail(where, "'type' must be \"string\" or \"integer\", not \"" .. kind .. "\"")
    end
    local r = { name = name, description = text(o, "description", where, false) or "", type = kind,
                display = text(o, "display", where, false) or "", values = {}, unsupported_values = {} }
    local function value(x, what)
      if kind == "string" then
        if type(x) ~= "string" then fail(where, what .. " must be string, not " .. type(x)) end
        return x
      end
      return integer_text(x, where, what)
    end
    if rawget(o, "default") == nil then fail(where, "'default' is missing") end
    r.default = value(rawget(o, "default"), "'default'")
    for j, x in ipairs(list(o, "values", where, false)) do r.values[j] = value(x, "values[" .. j .. "]") end
    for j, x in ipairs(list(o, "unsupported_values", where, false)) do
      r.unsupported_values[j] = value(x, "unsupported_values[" .. j .. "]")
    end
    for _, limit in ipairs({ "min", "max" }) do
      local x = rawget(o, limit)
      if x ~= nil then
        integer_text(x, where, "'" .. limit .. "'")
        r[limit] = x
      end
    end
    out[#out + 1] = r
  end
  return out
end

local function abbreviations_of(decl)
  local out = {}
  for i, v in ipairs(list(decl, "abbreviations", "declaration", false)) do
    local where = "abbreviations[" .. i .. "]"
    if type(v) == "string" then
      out[i] = { upper = v, shown = "", also_word = false }
    elseif type(v) == "table" then
      keys(v, set({ "upper", "shown", "also_word" }), where)
      out[i] = { upper = text(v, "upper", where, true), shown = text(v, "shown", where, false) or "",
                 also_word = flag(v, "also_word", where) }
    else
      fail(where, "must be string or table, not " .. type(v))
    end
  end
  return out
end

local function normalize(decl)
  if type(decl) ~= "table" then fail("declaration", "must be table, not " .. type(decl)) end
  keys(decl, set({ "name", "version", "channels", "inputs", "outputs", "options", "unsupported_options", "classes",
                   "rows", "allows_shared_channels", "check_capture", "look_ahead", "decode", "display",
                   "abbreviations", "protocols" }),
       "declaration")
  local d = { name = text(decl, "name", "declaration", true) }
  d.version = text(decl, "version", "declaration", false)
  d.display = text(decl, "display", "declaration", false) or ""
  d.abbreviations = abbreviations_of(decl)

  d.channels = {}
  for i, v in ipairs(list(decl, "channels", "declaration", false)) do
    local where = "channels[" .. i .. "]"
    local c = entry(v, where)
    keys(c, set({ "name", "description", "required", "display" }), where)
    d.channels[i] = { name = text(c, "name", where, true), description = text(c, "description", where, false) or "",
                      required = flag(c, "required", where), display = text(c, "display", where, false) or "" }
  end

  d.inputs = inputs_of(decl)
  d.outputs = outputs_of(decl)

  d.options = options_of(decl)

  d.unsupported_options = {}
  for i, v in ipairs(list(decl, "unsupported_options", "declaration", false)) do
    local where = "unsupported_options[" .. i .. "]"
    if type(v) ~= "string" or v == "" then fail(where, "must be option's name") end
    d.unsupported_options[i] = v
  end

  d.protocols = {}
  for i, v in ipairs(list(decl, "protocols", "declaration", false)) do
    local where = "protocols[" .. i .. "]"
    if type(v) ~= "string" or v == "" then fail(where, "must be decoder's name") end
    d.protocols[i] = v
  end

  d.classes = {}
  for i, v in ipairs(list(decl, "classes", "declaration", true)) do
    local where = "classes[" .. i .. "]"
    local c = entry(v, where)
    keys(c, set({ "id", "name", "format", "digits", "digits_option", "role" }), where)
    local r = { id = id(c, "id", where), name = text(c, "name", where, false) or "" }
    local f = text(c, "format", where, false) or "none"
    r.format = FORMATS[f]
    if r.format == nil then
      fail(where, "'format' must be \"none\", \"hex\", \"address7\", \"n_of_m\", \"decimal\", \"n_of_m_bytes\" or "
           .. "\"bytes\", not \"" .. f .. "\"")
    end
    local digits = rawget(c, "digits")
    if digits ~= nil then
      if f ~= "hex" then fail(where, "'digits' is for \"hex\" class") end
      if not whole(digits, 0, 17) then fail(where, "'digits' must be whole number from 0 to 16") end
    end
    r.digits = digits or 0
    local digits_option = text(c, "digits_option", where, false)
    if digits_option ~= nil and f ~= "hex" then fail(where, "'digits_option' is for \"hex\" class") end
    r.digits_option = digits_option
    local role = text(c, "role", where, false) or "none"
    r.role = ROLES[role]
    if r.role == nil then
      fail(where, "'role' must be \"none\", \"data\", \"start\", \"stop\", \"ok\", \"error\", \"control\", "
           .. "\"address\", \"bit\" or \"info\", not \"" .. role .. "\"")
    end
    d.classes[i] = r
  end

  d.rows = {}
  d.row_sources = {}
  for i, v in ipairs(list(decl, "rows", "declaration", true)) do
    local where = "rows[" .. i .. "]"
    local c = entry(v, where)
    keys(c, set({ "id", "name", "source" }), where)
    local r = { id = id(c, "id", where), name = text(c, "name", where, true) }
    d.rows[i] = r
    if rawget(c, "source") ~= nil then
      d.row_sources[#d.row_sources + 1] = row_source_of(r.id, rawget(c, "source"), where .. ".source")
    end
  end

  d.allows_shared_channels = flag(decl, "allows_shared_channels", "declaration")

  d.decode = rawget(decl, "decode")
  if type(d.decode) ~= "function" then fail("declaration", "'decode' must be function") end
  d.check_capture = rawget(decl, "check_capture")
  if d.check_capture ~= nil and type(d.check_capture) ~= "function" then
    fail("declaration", "'check_capture' must be function")
  end
  d.look_ahead = rawget(decl, "look_ahead")
  if d.look_ahead ~= nil and type(d.look_ahead) ~= "function" then
    fail("declaration", "'look_ahead' must be function")
  end
  return d
end

function H.new_env(refuse_declaration)
  local record = {}
  local env = pick(_G, GLOBALS)
  env.unpack = unpack
  env.collectgarbage = limited_collectgarbage
  env.string = pick(string, STRING)
  env.table = pick(table, TABLE)
  env.math = pick(math, MATH)
  env.bit = pick(bit, BIT)
  -- WHY: Setting the string metatable's __index makes method calls on strings reach only the restricted string table.
  local string_meta = getmetatable("")
  if string_meta then string_meta.__index = env.string end
  string.dump = nil
  env.decoder = function(decl)
    if record.decl ~= nil then
      error("decoder: script declares one decoder, and this one has declared '" .. record.decl.name
            .. "' already", 2)
    end
    local ok, d = pcall(normalize, decl)
    if not ok then error("decoder: " .. tostring(d), 2) end
    local refusal = refuse_declaration(d)
    if refusal ~= nil then error("decoder: " .. refusal, 2) end
    record.decl = d
  end
  return env, record
end

return H
