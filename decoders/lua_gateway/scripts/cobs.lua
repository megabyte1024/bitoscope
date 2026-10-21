-- SPDX-License-Identifier: MIT

local ROW_DATA, ROW_PACKETS, ROW_ERRORS = 0, 1, 2

local CLASS_DATA = 0
local CLASS_PACKET = 1
local CLASS_DELIMITER = 2
local CLASS_DELIMITER_INSIDE_BLOCK = 3
local CLASS_PACKET_TRUNCATED = 4
local CLASS_DAMAGED_BYTE = 5
local CLASS_DAMAGED_PACKET = 6

local U32 = 4294967296

local function decode(sig, emit, out)
  local input = sig.inputs["in"]
  local put = out.bytes
  local delimiter = sig.facts.options.delimiter
  local DATA, BOUNDARY, DAMAGED = sig.DATA, sig.BOUNDARY, sig.DAMAGED
  local bxor = bit.bxor
  local settle = sig.settle

  local open = false
  local packet_begin = 0
  local last_end = 0
  local encoded = 0
  local decoded = 0
  local first_damaged = nil
  local block_begin = 0
  local block_total = 0
  local remaining = 0
  local pending_zero = false

  local function reset()
    open = false
    encoded = 0
    decoded = 0
    first_damaged = nil
    remaining = 0
    pending_zero = false
  end
  local function damaged_packet()
    if first_damaged ~= nil then emit(packet_begin, last_end, ROW_ERRORS, CLASS_DAMAGED_PACKET, first_damaged) end
  end
  local function truncate(boundary_begin, boundary_end)
    emit(packet_begin, last_end, ROW_ERRORS, CLASS_PACKET_TRUNCATED, 0)
    damaged_packet()
    if put ~= nil then put(boundary_begin, boundary_end, BOUNDARY, 0, DAMAGED) end
    reset()
  end

  while true do
    local b_at, e_at, kind, value, flags = input.next()
    if b_at == nil then break end
    local watermark = open and packet_begin or b_at
    settle(watermark)
    if kind == BOUNDARY then
      if open then truncate(b_at, e_at) end
    elseif kind == DATA then
      local damaged = (flags % 2 == 1) and DAMAGED or 0
      local b = bxor(value % 256, delimiter)
      if damaged ~= 0 then emit(b_at, e_at, ROW_ERRORS, CLASS_DAMAGED_BYTE, 0) end

      if b == 0 then
        if open then
          if damaged ~= 0 and first_damaged == nil then first_damaged = encoded end
          local boundary_flags = damaged
          if remaining > 0 then
            local n_of_m = (block_total - remaining) + block_total * U32
            local e = b_at > block_begin and b_at or block_begin + 1
            emit(block_begin, e, ROW_ERRORS, CLASS_DELIMITER_INSIDE_BLOCK, n_of_m)
            boundary_flags = DAMAGED
          end
          emit(packet_begin, last_end, ROW_PACKETS, CLASS_PACKET, decoded)
          damaged_packet()
          if put ~= nil then put(b_at, e_at, BOUNDARY, 0, boundary_flags) end
          reset()
        end
        emit(b_at, e_at, ROW_PACKETS, CLASS_DELIMITER, 0)
      else
        if not open then
          open = true
          packet_begin = b_at
        end
        if damaged ~= 0 and first_damaged == nil then first_damaged = encoded end
        encoded = encoded + 1
        last_end = e_at
        if remaining == 0 then
          if pending_zero then
            emit(b_at, e_at, ROW_DATA, CLASS_DATA, 0)
            if put ~= nil then put(b_at, e_at, DATA, 0, damaged) end
            decoded = decoded + 1
          end
          block_begin = b_at
          block_total = b - 1
          remaining = block_total
          pending_zero = b ~= 0xFF
        else
          emit(b_at, e_at, ROW_DATA, CLASS_DATA, b)
          if put ~= nil then put(b_at, e_at, DATA, b, damaged) end
          decoded = decoded + 1
          remaining = remaining - 1
        end
      end
    end
  end
  if open then truncate(last_end - 1, last_end) end
end

decoder {
  name = "cobs",
  version = "1.0.0",
  inputs = {
    { name = "in", description = "encoded bytes", type = "words", width = 8, required = true, display = "Input" },
  },
  options = {
    { name = "delimiter", description = "byte that ends packet; non-zero: every byte is XORed with it",
      type = "integer", default = 0, min = 0, max = 255, display = "Delimiter" },
  },
  outputs = {
    { name = "bytes", description = "decoded bytes, boundary at each packet end", type = "words", width = 8,
      carries = "cobs" },
  },
  classes = {
    { id = CLASS_DATA, name = "", format = "hex", digits = 2, role = "data" },
    { id = CLASS_PACKET, name = "Packet", format = "decimal", role = "control" },
    { id = CLASS_DELIMITER, name = "Delimiter", role = "stop" },
    { id = CLASS_DELIMITER_INSIDE_BLOCK, name = "Delimiter inside block", format = "n_of_m_bytes", role = "error" },
    { id = CLASS_PACKET_TRUNCATED, name = "Packet truncated", role = "error" },
    { id = CLASS_DAMAGED_BYTE, name = "Damaged byte", role = "error" },
    { id = CLASS_DAMAGED_PACKET, name = "Damaged packet", format = "decimal", role = "error" },
  },
  rows = {
    { id = ROW_DATA, name = "Data" }, { id = ROW_PACKETS, name = "Packets" }, { id = ROW_ERRORS, name = "Errors" },
  },
  decode = decode,
}
