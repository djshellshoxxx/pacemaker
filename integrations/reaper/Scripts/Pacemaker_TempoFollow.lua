-- @description Pacemaker tempo follow (OSC -> REAPER project tempo)
-- @version 0.1.0
-- @about
--   Listens on a local UDP port for Pacemaker's OSC output (/pacemaker/tempo f)
--   and sets the REAPER project tempo with SetCurrentBPM, with smoothing and a
--   deadband. Run the script to start it, run it again to stop it (the action
--   shows as toggled on while running).
--
--   Needs a UDP socket library. REAPER has none built in. This script uses the
--   ReaPack package "Mavriq Lua Batteries" (provides require("socket.core")).
--   If it is not installed the script prints instructions and exits; the
--   alternative that needs no script is Pacemaker.ReaperOSC (see README.md).
--
-- Settings: edit the CONFIG table below.

local CONFIG = {
  port = 9000,              -- must equal the OSC port set in Pacemaker's OSC output
  bind = "127.0.0.1",       -- use "0.0.0.0" if Pacemaker runs on another machine
  root = "/pacemaker",      -- Pacemaker's OSC root
  alpha = 0.35,             -- smoothing, 1 = off
  deadband = 0.05,          -- BPM
  minInterval = 0.10,       -- seconds between tempo changes
  minBpm = 30, maxBpm = 300,
  confidenceGate = 0.35,
  verbose = false,          -- print every applied tempo to the ReaScript console
}

local function msg(s) reaper.ShowConsoleMsg("[Pacemaker] " .. s .. "\n") end

-- locate sibling modules in Scripts/Pacemaker_lib/
local here = (debug.getinfo(1, "S").source:match("^@(.*[/\\])") or "")
package.path = here .. "Pacemaker_lib/?.lua;" .. package.path
local okOsc, osc = pcall(require, "pm_osc")
local okFollow, follow = pcall(require, "pm_follow")
if not (okOsc and okFollow) then
  msg("Cannot load Pacemaker_lib next to this script (looked in '" .. here .. "Pacemaker_lib'). "
      .. "Copy the whole Scripts folder content, see README.md.")
  return
end

-- socket library: Mavriq Lua Batteries first (ReaPack), then whatever require finds.
local function loadSocket()
  local header = reaper.GetResourcePath() ..
    "/Scripts/Mavriq ReaScript Repository/Various/Mavriq-Lua-Batteries/batteries_header.lua"
  local f = io.open(header, "r")
  if f then f:close(); pcall(dofile, header) end
  local ok, sock = pcall(require, "socket.core")
  if ok then return sock end
  return nil
end

local socket = loadSocket()
if not socket then
  msg("No UDP socket library available, so this script cannot listen for OSC.\n"
      .. "  Option A: in REAPER, Extensions > ReaPack > Import repositories..., add\n"
      .. "            https://github.com/mavriq-dev/mavriq-reascript-repository/raw/master/index.xml\n"
      .. "            (verify the URL on the Mavriq GitHub page), then Browse packages, install\n"
      .. "            'Mavriq Lua Batteries', restart REAPER and run this script again.\n"
      .. "  Option B (no script, no dependency): use Pacemaker.ReaperOSC with REAPER's built-in\n"
      .. "            OSC control surface. See integrations/reaper/README.md.")
  return
end

local udp = socket.udp()
udp:settimeout(0)
local ok, err = udp:setsockname(CONFIG.bind, CONFIG.port)
if not ok then
  msg("Cannot listen on " .. CONFIG.bind .. ":" .. CONFIG.port .. " (" .. tostring(err) .. "). "
      .. "Is another program (or REAPER's OSC surface) using the port?")
  return
end

local follower = follow.new{
  root = CONFIG.root, alpha = CONFIG.alpha, deadband = CONFIG.deadband,
  minInterval = CONFIG.minInterval, minBpm = CONFIG.minBpm, maxBpm = CONFIG.maxBpm,
  confidenceGate = CONFIG.confidenceGate,
}

-- Toggle behaviour: running the script again terminates this instance.
if reaper.set_action_options then reaper.set_action_options(1) end
local _, _, sectionID, cmdID = reaper.get_action_context()
local function setToggle(state)
  if cmdID and cmdID ~= 0 then
    reaper.SetToggleCommandState(sectionID, cmdID, state)
    reaper.RefreshToolbar2(sectionID, cmdID)
  end
end
setToggle(1)
reaper.atexit(function() setToggle(0); udp:close() end)
msg("Listening for " .. CONFIG.root .. "/tempo on " .. CONFIG.bind .. ":" .. CONFIG.port ..
    ". Run the script again to stop.")

local function loop()
  local now = reaper.time_precise()
  for _ = 1, 64 do                       -- drain at most 64 datagrams per cycle
    local data = udp:receive()
    if not data then break end
    local msgs = osc.decode(data)
    if msgs then
      for _, m in ipairs(msgs) do follower:onMessage(m, now) end
    end
  end
  local bpm = follower:poll(now)
  if bpm then
    reaper.SetCurrentBPM(0, bpm, false)  -- project 0 = active project, no undo point
    follower:applied(bpm, now)
    if CONFIG.verbose then msg(string.format("tempo -> %.2f", bpm)) end
  end
  reaper.defer(loop)
end

loop()
