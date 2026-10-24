-- pm_osc.lua : minimal OSC 1.0 decoder in pure Lua (5.3+ : uses string.unpack).
-- No dependencies. Decodes messages and (nested) bundles.
--
--   local osc = require("pm_osc")
--   local msgs = osc.decode(packetString)   -- list of {address=, types=, args={...}, timetag=}
--   -- on malformed input: returns nil, "reason"
--
-- Supported type tags: i f s b h d t T F N I c r m S (c,r,m,S are returned as
-- integer / string; unknown tags make the packet fail rather than guess).

local M = {}

local unpack = string.unpack

local function readString(data, pos)
  local z = data:find("\0", pos, true)
  if not z then return nil, "unterminated string" end
  local s = data:sub(pos, z - 1)
  local padded = ((z - pos + 1) + 3) // 4 * 4         -- includes terminator, padded to 4
  if pos + padded - 1 > #data then return nil, "string padding past end" end
  return s, pos + padded
end

local function need(data, pos, n)
  if pos + n - 1 > #data then return false end
  return true
end

local function decodeMessage(data, pos, last, timetag)
  local sub = data:sub(pos, last)
  local address, p = readString(sub, 1)
  if not address then return nil, "bad address" end
  if address:sub(1, 1) ~= "/" then return nil, "address must start with /" end
  local types = ""
  if p <= #sub then                                    -- type tag string is optional in old OSC
    local t, p2 = readString(sub, p)
    if not t then return nil, "bad type tag string" end
    if t:sub(1, 1) ~= "," then return nil, "missing ',' in type tags" end
    types, p = t:sub(2), p2
  end
  local args = {}
  for i = 1, #types do
    local c = types:sub(i, i)
    if c == "i" then
      if not need(sub, p, 4) then return nil, "truncated int32" end
      args[#args + 1], p = unpack(">i4", sub, p)
    elseif c == "f" then
      if not need(sub, p, 4) then return nil, "truncated float32" end
      args[#args + 1], p = unpack(">f", sub, p)
    elseif c == "s" or c == "S" then
      local s, p2 = readString(sub, p)
      if not s then return nil, "bad string arg" end
      args[#args + 1], p = s, p2
    elseif c == "b" then
      if not need(sub, p, 4) then return nil, "truncated blob size" end
      local n; n, p = unpack(">I4", sub, p)
      if not need(sub, p, n) then return nil, "truncated blob" end
      args[#args + 1] = sub:sub(p, p + n - 1)
      p = p + (n + 3) // 4 * 4
    elseif c == "h" then
      if not need(sub, p, 8) then return nil, "truncated int64" end
      args[#args + 1], p = unpack(">i8", sub, p)
    elseif c == "t" then
      if not need(sub, p, 8) then return nil, "truncated timetag" end
      args[#args + 1], p = unpack(">I8", sub, p)
    elseif c == "d" then
      if not need(sub, p, 8) then return nil, "truncated float64" end
      args[#args + 1], p = unpack(">d", sub, p)
    elseif c == "c" or c == "r" then
      if not need(sub, p, 4) then return nil, "truncated 32-bit arg" end
      args[#args + 1], p = unpack(">I4", sub, p)
    elseif c == "m" then
      if not need(sub, p, 4) then return nil, "truncated midi arg" end
      args[#args + 1] = sub:sub(p, p + 3); p = p + 4
    elseif c == "T" then args[#args + 1] = true
    elseif c == "F" then args[#args + 1] = false
    elseif c == "N" then args[#args + 1] = nil
    elseif c == "I" then args[#args + 1] = math.huge
    else
      return nil, "unsupported type tag '" .. c .. "'"
    end
  end
  return { address = address, types = types, args = args, timetag = timetag }
end

local function decodeInto(data, pos, last, out, depth)
  if depth > 4 then return nil, "bundle nesting too deep" end
  if data:sub(pos, pos + 7) == "#bundle\0" then
    if last - pos + 1 < 16 then return nil, "truncated bundle header" end
    local timetag = unpack(">I8", data, pos + 8)
    local p = pos + 16
    while p <= last do
      if last - p + 1 < 4 then return nil, "truncated bundle element size" end
      local size; size, p = unpack(">I4", data, p)
      if size == 0 or p + size - 1 > last then return nil, "bad bundle element size" end
      local ok, err
      if data:sub(p, p + 7) == "#bundle\0" then
        ok, err = decodeInto(data, p, p + size - 1, out, depth + 1)
      else
        local m, e = decodeMessage(data, p, p + size - 1, timetag)
        ok, err = m, e
        if m then out[#out + 1] = m end
      end
      if not ok then return nil, err end
      p = p + size
    end
    return true
  end
  local m, e = decodeMessage(data, pos, last, 1)
  if not m then return nil, e end
  out[#out + 1] = m
  return true
end

-- Returns list of messages, or nil + error string.
function M.decode(data)
  if type(data) ~= "string" or #data < 4 then return nil, "packet too short" end
  local out = {}
  local ok, err = decodeInto(data, 1, #data, out, 0)
  if not ok then return nil, err end
  return out
end

-- Test helper / encoder for a single message (floats are float32).
function M.encodeMessage(address, types, ...)
  local function pad(s) s = s .. "\0"; return s .. string.rep("\0", (4 - #s % 4) % 4) end
  local parts = { pad(address), pad("," .. types) }
  local args = { ... }
  for i = 1, #types do
    local c = types:sub(i, i)
    if c == "i" then parts[#parts + 1] = string.pack(">i4", args[i])
    elseif c == "f" then parts[#parts + 1] = string.pack(">f", args[i])
    elseif c == "s" then parts[#parts + 1] = pad(args[i])
    else error("encodeMessage: unsupported tag " .. c) end
  end
  return table.concat(parts)
end

function M.encodeBundle(timetag, elements)
  local parts = { "#bundle\0", string.pack(">I8", timetag) }
  for _, e in ipairs(elements) do parts[#parts + 1] = string.pack(">I4", #e) .. e end
  return table.concat(parts)
end

return M
