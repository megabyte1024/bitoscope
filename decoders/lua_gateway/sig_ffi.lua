-- SPDX-License-Identifier: MIT

-- WHY: SDK accessors are called through a struct of function pointers via ffi.cast: an FFI call compiles into
-- a LuaJIT trace, where a classic C function would end the trace.

local ffi = require("ffi")
local bit = require("bit")
local tonumber, tostring, type, error, floor, pairs = tonumber, tostring, type, error, math.floor, pairs

ffi.cdef [[
typedef struct bitoscope_decode_input bitoscope_decode_input;
typedef struct bitoscope_annotations bitoscope_annotations;
typedef struct bitoscope_stream_item {
  uint64_t begin;
  uint64_t end;
  uint64_t value;
  uint32_t kind;
  uint32_t flags;
} bitoscope_stream_item;
typedef struct bitoscope_watched_line {
  uint32_t channel;
  int edge;
} bitoscope_watched_line;
typedef struct bitoscope_line_change {
  uint64_t at;
  uint32_t changed;
  uint32_t before;
  uint32_t after;
  uint32_t sampled;
} bitoscope_line_change;
typedef struct bitoscope_lua_api {
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
} bitoscope_lua_api;
]]

local OK, NONE = 0, -200
local CHANGE_LINES = 32
local CHANGE_BATCH = 64
local POSITION_MAX = 9007199254740991
local U32_MAX = 4294967295
local NO_LIMIT = ffi.new("uint64_t", -1)
local POSITION = "whole number from 0 to 2^53 - 1"
local VALUE = "whole number from 0 to 2^53 - 1, or byte array"
local BIG_NUMBER_HINT = "pass byte array for value of 2^53 or more"
local BYTE_ARRAY_LONGEST_ITEM = 8
local LOW_BYTES = { 1, 256, 65536, 16777216 }
local U32 = "whole number from 0 to 2^32 - 1"
local COUNT = "whole number from 0 to 32"
local KIND = "sig.RISING, sig.FALLING or sig.ANY"

local function bad(name, fn, expected, x)
  error("bad argument '" .. name .. "' to " .. fn .. " (" .. expected .. " expected, got " .. tostring(x) .. ")", 4)
end
local function position(x, name, fn)
  if type(x) ~= "number" or not (x >= 0 and x <= POSITION_MAX) or x ~= floor(x) then bad(name, fn, POSITION, x) end
  return x
end
local function u32(x, name, fn)
  if type(x) ~= "number" or not (x >= 0 and x <= U32_MAX) or x ~= floor(x) then bad(name, fn, U32, x) end
  return x
end
local function count(x, name, fn)
  if type(x) ~= "number" or not (x >= 0 and x <= 32) or x ~= floor(x) then bad(name, fn, COUNT, x) end
  return x
end
local function kind(x, name, fn)
  if x ~= 0 and x ~= 1 and x ~= 2 then bad(name, fn, KIND, x) end
  return x
end
local function value(x, name, fn)
  if type(x) == "number" and x >= 0 and x <= POSITION_MAX and x == floor(x) then return x end
  if type(x) == "number" and x > POSITION_MAX then
    error("bad argument '" .. name .. "' to " .. fn .. " (" .. VALUE .. " expected, got " .. tostring(x) .. "; " ..
          BIG_NUMBER_HINT .. ")", 3)
  end
  bad(name, fn, VALUE, x)
end

local function bad_bytes(name, fn, what)
  error("bad argument '" .. name .. "' to " .. fn .. " (" .. what .. ")", 4)
end

local function byte_array(t, name, fn, most)
  local n = #t
  if n == 0 then bad_bytes(name, fn, "byte array expected, but it has no bytes") end
  if most ~= nil and n > most then
    bad_bytes(name, fn, "byte array of at most " .. most .. " bytes expected, but it has " .. n)
  end
  for i = 1, n do
    local b = t[i]
    if type(b) ~= "number" or b < 0 or b > 255 or b ~= floor(b) then
      bad_bytes(name, fn, "byte array expected, but byte " .. i .. " is " .. tostring(b) ..
                          ", not whole number from 0 to 255")
    end
  end
  return n
end

local function u64_of_bytes(t, n)
  local lo, hi = 0, 0
  for i = 1, n do
    if i <= 4 then lo = lo + t[i] * LOW_BYTES[i] else hi = hi + t[i] * LOW_BYTES[i - 4] end
  end
  return ffi.new("uint64_t", hi) * 4294967296ULL + lo
end

local function bytes_of_u64(v)
  local lo = tonumber(bit.band(v, 0xFFFFFFFF))
  local hi = tonumber(bit.rshift(v, 32))
  local t = {}
  for i = 1, 4 do
    t[i] = lo % 256
    lo = (lo - t[i]) / 256
    t[i + 4] = hi % 256
    hi = (hi - t[i + 4]) / 256
  end
  return t
end

return function(api, input, annotations, facts, outputs, inputs)
  local C = ffi.cast("const bitoscope_lua_api*", api)
  local inp = ffi.cast("bitoscope_decode_input*", input)
  local out = ffi.cast("bitoscope_annotations*", annotations)
  local at = ffi.new("uint64_t[1]")
  local last = ffi.new("uint64_t[1]")
  local which = ffi.new("uint32_t[1]")
  local final = ffi.new("int[1]")
  local level = ffi.new("int[1]")
  local item = ffi.new("bitoscope_stream_item[1]")
  local scratch = ffi.new("uint8_t[?]", 256)
  local scratch_size = 256

  local function failed()
    local reason = C.decode_error(inp)
    error(reason ~= nil and ffi.string(reason) or "decode job has failed", 3)
  end

  local sig = { RISING = 0, FALLING = 1, ANY = 2, facts = facts,
                DATA = 0, BOUNDARY = 1, BEGIN = 2, DAMAGED = 1, NO_MOSI = 2, NO_MISO = 4 }

  function sig.level(channel, sample)
    local r = C.level(inp, u32(channel, "channel", "sig.level"), position(sample, "sample", "sig.level"), level)
    if r == OK then return level[0] end
    if r == NONE then return nil end
    failed()
  end

  function sig.next_edge(channel, from, edge, limit)
    local r
    if limit == nil then
      r = C.next_edge(inp, u32(channel, "channel", "sig.next_edge"), position(from, "from", "sig.next_edge"),
                      kind(edge, "kind", "sig.next_edge"), at)
    else
      r = C.next_edge_limit(inp, u32(channel, "channel", "sig.next_edge"), position(from, "from", "sig.next_edge"),
                            kind(edge, "kind", "sig.next_edge"), position(limit, "limit", "sig.next_edge"), at)
    end
    if r == OK then return tonumber(at[0]) end
    if r == NONE then return nil end
    failed()
  end

  function sig.next_change2(channel_a, channel_b, from, limit)
    local r
    if limit == nil then
      r = C.next_change2(inp, u32(channel_a, "channel_a", "sig.next_change2"),
                         u32(channel_b, "channel_b", "sig.next_change2"),
                         position(from, "from", "sig.next_change2"), at, which)
    else
      r = C.next_change2_limit(inp, u32(channel_a, "channel_a", "sig.next_change2"),
                               u32(channel_b, "channel_b", "sig.next_change2"),
                               position(from, "from", "sig.next_change2"),
                               position(limit, "limit", "sig.next_change2"), at, which)
    end
    if r == OK then return tonumber(at[0]), tonumber(which[0]) end
    if r == NONE then return nil end
    failed()
  end

  function sig.changes(spec)
    local fn = "sig.changes"
    if type(spec) ~= "table" or type(spec.watch) ~= "table" or #spec.watch < 1 then
      error("bad argument to sig.changes (table { watch = { { channel, edge }, ... }, sample = { channel, ... } } " ..
            "expected, with 1 or more watched lines)", 2)
    end
    local watch, sample = spec.watch, spec.sample or {}
    local n, m = #watch, #sample
    if n + m > CHANGE_LINES then
      error("bad argument to sig.changes (at most " .. CHANGE_LINES .. " watched and sampled lines together, got " ..
            n .. " and " .. m .. ")", 2)
    end
    local lines = ffi.new("bitoscope_watched_line[?]", n)
    for i = 1, n do
      local line = watch[i]
      lines[i - 1].channel = u32(line[1], "watch[" .. i .. "] channel", fn)
      lines[i - 1].edge = line[2] == nil and sig.ANY or kind(line[2], "watch[" .. i .. "] edge", fn)
    end
    local sampled = ffi.new("uint32_t[?]", m)
    for j = 1, m do sampled[j - 1] = u32(sample[j], "sample[" .. j .. "]", fn) end
    local batch = ffi.new("bitoscope_line_change[?]", CHANGE_BATCH)
    local got = ffi.new("uint32_t[1]")
    local shift = 2 ^ n
    local index, count, from, previous, finished = 0, 0, 0, 0, false
    return function()
      if index >= count then
        if finished then return nil end
        if C.changes(inp, lines, n, sampled, m, from, previous, batch, CHANGE_BATCH, got) ~= OK then failed() end
        index, count = 0, got[0]
        finished = count < CHANGE_BATCH
        if count == 0 then return nil end
        local last = batch[count - 1]
        from, previous = tonumber(last.at) + 1, last.after
      end
      local change = batch[index]
      index = index + 1
      return tonumber(change.at), change.changed, change.after + change.sampled * shift
    end
  end

  function sig.bits(channel, first, n)
    local r = C.bits(inp, u32(channel, "channel", "sig.bits"), position(first, "first", "sig.bits"),
                     count(n, "n", "sig.bits"), at)
    if r == OK then return tonumber(at[0]) end
    if r == NONE then return nil end
    failed()
  end

  function sig.sample_at(channel, start, num, den, k0, n)
    local r = C.sample_at(inp, u32(channel, "channel", "sig.sample_at"), position(start, "start", "sig.sample_at"),
                          position(num, "num", "sig.sample_at"), position(den, "den", "sig.sample_at"),
                          position(k0, "k0", "sig.sample_at"), count(n, "n", "sig.sample_at"), at)
    if r == OK then return tonumber(at[0]) end
    if r == NONE then return nil end
    failed()
  end

  function sig.clocked(clock, data, from, edge, n, limit)
    local lim = NO_LIMIT
    if limit ~= nil then lim = position(limit, "limit", "sig.clocked") end
    local r = C.clocked(inp, u32(clock, "clock", "sig.clocked"), u32(data, "data", "sig.clocked"),
                        position(from, "from", "sig.clocked"), kind(edge, "edge", "sig.clocked"),
                        count(n, "n", "sig.clocked"), lim, at, last)
    if r >= 0 then return r, tonumber(at[0]), tonumber(last[0]) end
    failed()
  end

  function sig.sample_count()
    local n = C.sample_count(inp, final)
    return tonumber(n), final[0] == 1
  end

  function sig.release_before(sample)
    if C.release_before(inp, position(sample, "sample", "sig.release_before")) ~= OK then failed() end
  end

  local settle_due = 0
  local total_samples = facts.total_samples

  function sig.settle(sample)
    position(sample, "sample", "sig.settle")
    if sample < settle_due and sample ~= total_samples then return end
    if C.settle(out, sample) ~= OK then failed() end
    settle_due = tonumber(C.settle_due(out))
  end

  local function emit(b, e, row, cls, v)
    if v == nil then v = 0 end
    local r
    if type(v) == "table" then
      local n = byte_array(v, "value", "emit")
      if n > scratch_size then
        scratch = ffi.new("uint8_t[?]", n)
        scratch_size = n
      end
      for i = 1, n do scratch[i - 1] = v[i] end
      r = C.emit_bytes(out, position(b, "begin", "emit"), position(e, "end", "emit"), u32(row, "row", "emit"),
                       u32(cls, "class", "emit"), scratch, n)
    else
      r = C.emit(out, position(b, "begin", "emit"), position(e, "end", "emit"), u32(row, "row", "emit"),
                 u32(cls, "class", "emit"), value(v, "value", "emit"))
    end
    if r ~= OK then failed() end
  end

  sig.inputs = {}
  for name, index in pairs(inputs) do
    sig.inputs[name] = {
      next = function()
        local r = C.next_item(inp, index, item)
        if r == OK then
          local it = item[0]
          local v = it.value
          if v <= POSITION_MAX then v = tonumber(v) else v = bytes_of_u64(v) end
          return tonumber(it.begin), tonumber(it["end"]), it.kind, v, it.flags
        end
        if r == NONE then return nil end
        failed()
      end,
    }
  end

  local put = {}
  for name, index in pairs(outputs) do
    local fn = "out." .. name
    put[name] = function(b, e, kind, v, flags)
      if v == nil then v = 0 end
      if flags == nil then flags = 0 end
      if type(v) == "table" then
        v = u64_of_bytes(v, byte_array(v, "value", fn, BYTE_ARRAY_LONGEST_ITEM))
      else
        v = value(v, "value", fn)
      end
      local r = C.put(out, index, position(b, "begin", fn), position(e, "end", fn), u32(kind, "kind", fn), v,
                      u32(flags, "flags", fn))
      if r ~= OK then failed() end
    end
  end

  return sig, emit, put
end
