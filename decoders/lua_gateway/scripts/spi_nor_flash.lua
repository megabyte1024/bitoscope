-- SPDX-License-Identifier: MIT

local ROW_COMMANDS, ROW_FIELDS, ROW_STATUS, ROW_ERRORS = 0, 1, 2, 3

local CLASS_OPCODE, CLASS_ADDRESS3, CLASS_ADDRESS4, CLASS_DUMMY, CLASS_DATA_BYTE = 0, 1, 2, 3, 4
local CLASS_MANUFACTURER, CLASS_MEMORY_TYPE, CLASS_CAPACITY, CLASS_DEVICE_ID, CLASS_UNIQUE_ID = 5, 6, 7, 8, 9
local CLASS_STATUS1, CLASS_STATUS2, CLASS_STATUS3 = 10, 11, 12
local CLASS_BUSY, CLASS_WEL, CLASS_QE, CLASS_SUS = 13, 14, 15, 16
local CLASS_UNKNOWN_OPCODE, CLASS_INCOMPLETE, CLASS_NOT_DECODABLE = 17, 18, 19
local CLASS_BEGAN_BEFORE, CLASS_DAMAGED, CLASS_NOT_CAPTURED = 20, 21, 22
local CLASS_FIRST_COMMAND = 32

local U32 = 4294967296

local A_NONE, A_MODE, A_THREE, A_FOUR = 0, 1, 2, 3

local D_NONE = 0
local D_MOSI_BYTES = 1
local D_MISO_BYTES = 2
local D_READ_STATUS = 3
local D_WRITE_STATUS = 4
local D_JEDEC = 5
local D_MFR_DEVICE = 6
local D_DEVICE_ID = 7
local D_UNIQUE_ID = 8
local D_NOT_DECODABLE = 9

local function cmd(opcode, name, addr, dummy, data, min_data, reg, opcode_alone_ok, all_after_opcode,
                   longer_unique_id)
  return { opcode = opcode, name = name, addr = addr, dummy = dummy, data = data, min_data = min_data,
           reg = reg or 0, opcode_alone_ok = opcode_alone_ok or false, all_after_opcode = all_after_opcode or false,
           longer_unique_id = longer_unique_id or false }
end

local COMMANDS = {
  cmd(0x06, "Write enable", A_NONE, 0, D_NONE, 0),
  cmd(0x50, "Volatile SR write enable", A_NONE, 0, D_NONE, 0),
  cmd(0x04, "Write disable", A_NONE, 0, D_NONE, 0),
  cmd(0xAB, "Release power-down / device ID", A_NONE, 3, D_DEVICE_ID, 1, 0, true),
  cmd(0x90, "Manufacturer/device ID", A_THREE, 0, D_MFR_DEVICE, 2),
  cmd(0x9F, "JEDEC ID", A_NONE, 0, D_JEDEC, 3),
  cmd(0x4B, "Read unique ID", A_NONE, 4, D_UNIQUE_ID, 8, 0, false, false, true),
  cmd(0x03, "Read data", A_MODE, 0, D_MISO_BYTES, 0),
  cmd(0x13, "Read data (4-byte address)", A_FOUR, 0, D_MISO_BYTES, 0),
  cmd(0x0B, "Fast read", A_MODE, 1, D_MISO_BYTES, 0),
  cmd(0x0C, "Fast read (4-byte address)", A_FOUR, 1, D_MISO_BYTES, 0),
  cmd(0x02, "Page program", A_MODE, 0, D_MOSI_BYTES, 1),
  cmd(0x12, "Page program (4-byte address)", A_FOUR, 0, D_MOSI_BYTES, 1),
  cmd(0x20, "Sector erase (4KB)", A_MODE, 0, D_NONE, 0),
  cmd(0x21, "Sector erase (4KB, 4-byte address)", A_FOUR, 0, D_NONE, 0),
  cmd(0x52, "Block erase (32KB)", A_MODE, 0, D_NONE, 0),
  cmd(0xD8, "Block erase (64KB)", A_MODE, 0, D_NONE, 0),
  cmd(0xDC, "Block erase (64KB, 4-byte address)", A_FOUR, 0, D_NONE, 0),
  cmd(0xC7, "Chip erase", A_NONE, 0, D_NONE, 0),
  cmd(0x60, "Chip erase", A_NONE, 0, D_NONE, 0),
  cmd(0x05, "Read status register 1", A_NONE, 0, D_READ_STATUS, 1, 1),
  cmd(0x01, "Write status register 1", A_NONE, 0, D_WRITE_STATUS, 1, 1),
  cmd(0x35, "Read status register 2", A_NONE, 0, D_READ_STATUS, 1, 2),
  cmd(0x31, "Write status register 2", A_NONE, 0, D_WRITE_STATUS, 1, 2),
  cmd(0x15, "Read status register 3", A_NONE, 0, D_READ_STATUS, 1, 3),
  cmd(0x11, "Write status register 3", A_NONE, 0, D_WRITE_STATUS, 1, 3),
  cmd(0xC8, "Read extended address register", A_NONE, 0, D_MISO_BYTES, 1),
  cmd(0xC5, "Write extended address register", A_NONE, 0, D_MOSI_BYTES, 1),
  cmd(0x5A, "Read SFDP register", A_THREE, 1, D_MISO_BYTES, 0),
  cmd(0x44, "Erase security register", A_MODE, 0, D_NONE, 0),
  cmd(0x42, "Program security register", A_MODE, 0, D_MOSI_BYTES, 1),
  cmd(0x48, "Read security register", A_MODE, 1, D_MISO_BYTES, 0),
  cmd(0x7E, "Global block lock", A_NONE, 0, D_NONE, 0),
  cmd(0x98, "Global block unlock", A_NONE, 0, D_NONE, 0),
  cmd(0x3D, "Read block lock", A_MODE, 0, D_MISO_BYTES, 1),
  cmd(0x36, "Individual block lock", A_MODE, 0, D_NONE, 0),
  cmd(0x39, "Individual block unlock", A_MODE, 0, D_NONE, 0),
  cmd(0x75, "Erase/program suspend", A_NONE, 0, D_NONE, 0),
  cmd(0x7A, "Erase/program resume", A_NONE, 0, D_NONE, 0),
  cmd(0xB9, "Power-down", A_NONE, 0, D_NONE, 0),
  cmd(0xB7, "Enter 4-byte address mode", A_NONE, 0, D_NONE, 0),
  cmd(0xE9, "Exit 4-byte address mode", A_NONE, 0, D_NONE, 0),
  cmd(0x66, "Enable reset", A_NONE, 0, D_NONE, 0),
  cmd(0x99, "Reset device", A_NONE, 0, D_NONE, 0),
  cmd(0x3B, "Fast read dual output", A_MODE, 1, D_NOT_DECODABLE, 0),
  cmd(0x3C, "Fast read dual output (4-byte address)", A_FOUR, 1, D_NOT_DECODABLE, 0),
  cmd(0x6B, "Fast read quad output", A_MODE, 1, D_NOT_DECODABLE, 0),
  cmd(0x6C, "Fast read quad output (4-byte address)", A_FOUR, 1, D_NOT_DECODABLE, 0),
  cmd(0x32, "Quad input page program", A_MODE, 0, D_NOT_DECODABLE, 0),
  cmd(0x34, "Quad input page program (4-byte address)", A_FOUR, 0, D_NOT_DECODABLE, 0),
  cmd(0xBB, "Fast read dual I/O", A_NONE, 0, D_NOT_DECODABLE, 0, 0, false, true),
  cmd(0xBC, "Fast read dual I/O (4-byte address)", A_NONE, 0, D_NOT_DECODABLE, 0, 0, false, true),
  cmd(0xEB, "Fast read quad I/O", A_NONE, 0, D_NOT_DECODABLE, 0, 0, false, true),
  cmd(0xEC, "Fast read quad I/O (4-byte address)", A_NONE, 0, D_NOT_DECODABLE, 0, 0, false, true),
  cmd(0x92, "Manufacturer/device ID dual I/O", A_NONE, 0, D_NOT_DECODABLE, 0, 0, false, true),
  cmd(0x94, "Manufacturer/device ID quad I/O", A_NONE, 0, D_NOT_DECODABLE, 0, 0, false, true),
  cmd(0x77, "Set burst with wrap", A_NONE, 0, D_NOT_DECODABLE, 0, 0, false, true),
}

local BY_OPCODE = {}
for i, c in ipairs(COMMANDS) do
  if BY_OPCODE[c.opcode] == nil then BY_OPCODE[c.opcode] = i - 1 end
end

local READS = { [D_MISO_BYTES] = true, [D_READ_STATUS] = true, [D_JEDEC] = true, [D_MFR_DEVICE] = true,
                [D_DEVICE_ID] = true, [D_UNIQUE_ID] = true }

local function decode(sig, emit)
  local input = sig.inputs["in"]
  local DATA, BOUNDARY, BEGIN = sig.DATA, sig.BOUNDARY, sig.BEGIN
  local DAMAGED, NO_MOSI, NO_MISO = sig.DAMAGED, sig.NO_MOSI, sig.NO_MISO
  local floor = math.floor
  local settle = sig.settle
  local address_bytes = sig.facts.options.address_bytes

  local function has(flags, flag) return floor(flags / flag) % 2 == 1 end

  local s = { open = false }

  local function begin_transfer(at, began_before)
    s = { open = true, began_before = began_before, no_mosi = false, begin = at, last_end = 0, count = 0,
          opcode = 0, command = nil, addr_bytes = 0, dummy = 0, field_begin = 0, address = 0,
          unique = {}, undecodable = false, undecodable_begin = 0, undecodable_end = 0, no_miso = false,
          no_miso_begin = 0, no_miso_end = 0 }
  end

  local function layout()
    local c = COMMANDS[s.command + 1]
    if c.addr == A_NONE then s.addr_bytes = 0
    elseif c.addr == A_MODE then s.addr_bytes = address_bytes
    elseif c.addr == A_THREE then s.addr_bytes = 3
    else s.addr_bytes = 4 end
    s.dummy = c.dummy + ((c.longer_unique_id and address_bytes == 4) and 1 or 0)
  end

  local function not_decodable(b, e)
    if not s.undecodable then s.undecodable_begin = b end
    s.undecodable = true
    s.undecodable_end = e
  end

  local function not_captured(b, e)
    if not s.no_miso then s.no_miso_begin = b end
    s.no_miso = true
    s.no_miso_end = e
  end

  local function status_register(reg, value, b, e)
    local raw = reg == 1 and CLASS_STATUS1 or (reg == 2 and CLASS_STATUS2 or CLASS_STATUS3)
    emit(b, e, ROW_STATUS, raw, value)
    if reg == 1 then
      if value % 2 == 1 then emit(b, e, ROW_STATUS, CLASS_BUSY, 0) end
      if floor(value / 2) % 2 == 1 then emit(b, e, ROW_STATUS, CLASS_WEL, 0) end
    elseif reg == 2 then
      if floor(value / 2) % 2 == 1 then emit(b, e, ROW_STATUS, CLASS_QE, 0) end
      if floor(value / 128) % 2 == 1 then emit(b, e, ROW_STATUS, CLASS_SUS, 0) end
    end
  end

  local function data_byte(c, d, b, e, mosi, miso, has_miso)
    if READS[c.data] and not has_miso then
      not_captured(b, e)
      return
    end
    local kind = c.data
    if kind == D_NONE then return
    elseif kind == D_NOT_DECODABLE then not_decodable(b, e)
    elseif kind == D_MOSI_BYTES then emit(b, e, ROW_FIELDS, CLASS_DATA_BYTE, mosi)
    elseif kind == D_MISO_BYTES then emit(b, e, ROW_FIELDS, CLASS_DATA_BYTE, miso)
    elseif kind == D_READ_STATUS then status_register(c.reg, miso, b, e)
    elseif kind == D_WRITE_STATUS then
      local last = c.reg == 1 and 2 or c.reg
      if c.reg + d <= last then
        status_register(c.reg + d, mosi, b, e)
      else
        emit(b, e, ROW_FIELDS, CLASS_DATA_BYTE, mosi)
      end
    elseif kind == D_JEDEC then
      if d < 3 then
        local cls = d == 0 and CLASS_MANUFACTURER or (d == 1 and CLASS_MEMORY_TYPE or CLASS_CAPACITY)
        emit(b, e, ROW_FIELDS, cls, miso)
      end
    elseif kind == D_MFR_DEVICE then
      emit(b, e, ROW_FIELDS, d % 2 == 0 and CLASS_MANUFACTURER or CLASS_DEVICE_ID, miso)
    elseif kind == D_DEVICE_ID then emit(b, e, ROW_FIELDS, CLASS_DEVICE_ID, miso)
    elseif kind == D_UNIQUE_ID then
      if d >= 8 then return end
      if d == 0 then s.field_begin = b end
      s.unique[d + 1] = miso
      if d == 7 then emit(s.field_begin, e, ROW_FIELDS, CLASS_UNIQUE_ID, s.unique) end
    end
  end

  local function data(b, e, value, flags)
    if not s.open then begin_transfer(b, true) end
    s.last_end = e
    if has(flags, DAMAGED) then emit(b, e, ROW_ERRORS, CLASS_DAMAGED, 0) end
    local k = s.count
    s.count = s.count + 1
    if s.began_before or s.no_mosi then return end
    if k == 0 and has(flags, NO_MOSI) then
      s.no_mosi = true
      return
    end
    local mosi = value % 256
    local miso = floor(value / U32) % 256
    local has_miso = not has(flags, NO_MISO)

    if k == 0 then
      emit(b, e, ROW_FIELDS, CLASS_OPCODE, mosi)
      s.opcode = mosi
      s.command = BY_OPCODE[mosi]
      if s.command ~= nil then layout() end
      return
    end
    if s.command == nil then return end
    local c = COMMANDS[s.command + 1]
    if c.all_after_opcode then
      not_decodable(b, e)
      return
    end
    if k < 1 + s.addr_bytes then
      if k == 1 then s.field_begin = b end
      s.address = s.address * 256 + mosi
      if k == s.addr_bytes then
        emit(s.field_begin, e, ROW_FIELDS, s.addr_bytes == 4 and CLASS_ADDRESS4 or CLASS_ADDRESS3, s.address)
      end
      return
    end
    if k < 1 + s.addr_bytes + s.dummy then
      if k == 1 + s.addr_bytes then s.field_begin = b end
      if k == s.addr_bytes + s.dummy then emit(s.field_begin, e, ROW_FIELDS, CLASS_DUMMY, s.dummy) end
      return
    end
    data_byte(c, k - (1 + s.addr_bytes + s.dummy), b, e, mosi, miso, has_miso)
  end

  local function finish(e)
    if s.began_before then
      emit(s.begin, e, ROW_ERRORS, CLASS_BEGAN_BEFORE, 0)
      return
    end
    if s.no_mosi then
      emit(s.begin, e, ROW_ERRORS, CLASS_NOT_CAPTURED, 0)
      return
    end
    if s.command == nil then
      emit(s.begin, e, ROW_ERRORS, CLASS_UNKNOWN_OPCODE, s.opcode)
      return
    end
    local c = COMMANDS[s.command + 1]
    emit(s.begin, e, ROW_COMMANDS, CLASS_FIRST_COMMAND + s.command, 0)
    local needed
    if c.opcode_alone_ok and s.count == 1 then
      needed = 1
    else
      needed = 1 + s.addr_bytes + s.dummy + c.min_data
    end
    if s.count < needed then emit(s.begin, e, ROW_ERRORS, CLASS_INCOMPLETE, s.count + needed * U32) end
    if s.undecodable then emit(s.undecodable_begin, s.undecodable_end, ROW_ERRORS, CLASS_NOT_DECODABLE, 0) end
    if s.no_miso then emit(s.no_miso_begin, s.no_miso_end, ROW_ERRORS, CLASS_NOT_CAPTURED, 0) end
    if c.opcode == 0xB7 then address_bytes = 4 end
    if c.opcode == 0xE9 then address_bytes = 3 end
  end

  local function end_transfer(b, e, flags)
    local stop = b ~= nil and e or s.last_end
    if s.count > 0 or s.began_before then finish(stop) end
    if b ~= nil and has(flags, DAMAGED) then emit(b, e, ROW_ERRORS, CLASS_DAMAGED, 0) end
    s.open = false
  end

  while true do
    local b, e, kind, value, flags = input.next()
    if b == nil then break end
    local watermark = s.open and s.begin or b
    settle(watermark)
    if kind == BEGIN then
      if s.open then end_transfer(nil) end
      begin_transfer(b, has(flags, DAMAGED))
    elseif kind == DATA then
      data(b, e, value, flags)
    elseif kind == BOUNDARY then
      if s.open then end_transfer(b, e, flags) end
    end
  end
  if s.open then end_transfer(nil) end
end

local classes = {
  { id = CLASS_OPCODE, name = "Opcode", format = "hex", hex_digits = 2, role = "control" },
  { id = CLASS_ADDRESS3, name = "Address", format = "hex", hex_digits = 6, role = "control" },
  { id = CLASS_ADDRESS4, name = "Address", format = "hex", hex_digits = 8, role = "control" },
  { id = CLASS_DUMMY, name = "Dummy", format = "decimal", role = "control" },
  { id = CLASS_DATA_BYTE, name = "Data byte", format = "hex", hex_digits = 2, role = "data" },
  { id = CLASS_MANUFACTURER, name = "Manufacturer", format = "hex", hex_digits = 2, role = "control" },
  { id = CLASS_MEMORY_TYPE, name = "Memory type", format = "hex", hex_digits = 2, role = "control" },
  { id = CLASS_CAPACITY, name = "Capacity", format = "hex", hex_digits = 2, role = "control" },
  { id = CLASS_DEVICE_ID, name = "Device ID", format = "hex", hex_digits = 2, role = "control" },
  { id = CLASS_UNIQUE_ID, name = "Unique ID", format = "bytes", role = "control" },
  { id = CLASS_STATUS1, name = "Status register 1", format = "hex", hex_digits = 2, role = "control" },
  { id = CLASS_STATUS2, name = "Status register 2", format = "hex", hex_digits = 2, role = "control" },
  { id = CLASS_STATUS3, name = "Status register 3", format = "hex", hex_digits = 2, role = "control" },
  { id = CLASS_BUSY, name = "BUSY", role = "info" },
  { id = CLASS_WEL, name = "WEL", role = "info" },
  { id = CLASS_QE, name = "QE", role = "info" },
  { id = CLASS_SUS, name = "SUS", role = "info" },
  { id = CLASS_UNKNOWN_OPCODE, name = "Unknown opcode", format = "hex", hex_digits = 2, role = "info" },
  { id = CLASS_INCOMPLETE, name = "Incomplete command", format = "n_of_m_bytes", role = "error" },
  { id = CLASS_NOT_DECODABLE, name = "Not decodable on single-line SPI", role = "info" },
  { id = CLASS_BEGAN_BEFORE, name = "Began before capture", role = "error" },
  { id = CLASS_DAMAGED, name = "Damaged", role = "error" },
  { id = CLASS_NOT_CAPTURED, name = "Data not captured", role = "error" },
}
for i, c in ipairs(COMMANDS) do
  classes[#classes + 1] = { id = CLASS_FIRST_COMMAND + i - 1, name = c.name, role = "control" }
end

decoder {
  name = "spi_nor_flash",
  display = "SPI NOR flash",
  version = "0.1.0",
  protocols = { "spi", "qspi" },
  inputs = {
    { name = "in", description = "SPI transfers", type = "spi-transfers", width = 8,
      display = "Input" },
  },
  options = {
    { name = "address_bytes", description = "address bytes at start of capture (B7h/E9h switch it)",
      type = "integer", default_value = 3, allowed_values = { 3, 4 }, display = "Address bytes" },
  },
  classes = classes,
  rows = {
    { id = ROW_COMMANDS, name = "Commands" }, { id = ROW_FIELDS, name = "Fields" },
    { id = ROW_STATUS, name = "Status bits" }, { id = ROW_ERRORS, name = "Errors" },
  },
  decode = decode,
}
