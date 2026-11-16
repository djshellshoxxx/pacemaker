-- Run:  lua5.4 tests/test_osc.lua    (from integrations/reaper/)
package.path = "Scripts/Pacemaker_lib/?.lua;" .. package.path
local osc = require("pm_osc")
local follow = require("pm_follow")

local fails, count = 0, 0
local function check(cond, name)
  count = count + 1
  if not cond then fails = fails + 1; print("FAIL: " .. name) end
end
local function near(a, b, e) return math.abs(a - b) <= (e or 1e-6) end
local function hex(s) return (s:gsub("..", function(h) return string.char(tonumber(h, 16)) end)) end

-- 1. Fixtures produced independently with Python struct (see tests/gen_fixtures.py)
--    /pacemaker/tempo ,f 120.5
local p = hex("2f706163656d616b65722f74656d706f00000000" .. "2c660000" .. "42f10000")
local m = assert(osc.decode(p))
check(#m == 1 and m[1].address == "/pacemaker/tempo" and near(m[1].args[1], 120.5), "single float message")


-- 1b. Bytes produced by the real C++ encoder (services/src/Osc.cpp encodeOscBundle / encodeOscPacket)
local cppBundle = hex("2362756e646c6500e8fe6f801f9acffa000000242f706163656d616b65722f62656174002c69696900000000000000070000000200000004000000202f706163656d616b65722f7374617465000000002c7300004c4f434b45440000")
local cm = assert(osc.decode(cppBundle))
check(#cm == 2 and cm[1].address == "/pacemaker/beat" and cm[1].args[1] == 7 and cm[1].args[2] == 2 and cm[1].args[3] == 4, "C++ bundle beat")
check(cm[2].address == "/pacemaker/state" and cm[2].args[1] == "LOCKED", "C++ bundle state")
check(cm[1].timetag == 0xe8fe6f801f9acffa, "C++ bundle NTP timetag")
local ct = assert(osc.decode(hex("2f706163656d616b65722f74656d706f000000002c66000042f68000")))
check(near(ct[1].args[1], 123.25), "C++ tempo packet")

-- 2. /beat iii in a bundle with timetag
local beat = osc.encodeMessage("/pacemaker/beat", "iii", 12, 3, 4)
local down = osc.encodeMessage("/pacemaker/downbeat", "i", 12)
local b = osc.encodeBundle(0xE0000000AABBCCDD, { beat, down })
local mb = assert(osc.decode(b))
check(#mb == 2, "bundle message count")
check(mb[1].address == "/pacemaker/beat" and mb[1].args[1] == 12 and mb[1].args[2] == 3 and mb[1].args[3] == 4, "beat args")
check(mb[1].timetag == 0xE0000000AABBCCDD or mb[1].timetag == (0xE0000000AABBCCDD), "bundle timetag kept")
check(mb[2].address == "/pacemaker/downbeat" and mb[2].args[1] == 12, "downbeat in bundle")

-- 3. string and negative int
local s = osc.encodeMessage("/pacemaker/state", "s", "LOCKED")
local ms = assert(osc.decode(s))
check(ms[1].args[1] == "LOCKED", "string arg")
local neg = assert(osc.decode(osc.encodeMessage("/x", "i", -5)))
check(neg[1].args[1] == -5, "negative int")

-- 4. string whose length is a multiple of 4 needs a full 4 byte NUL pad
local s4 = assert(osc.decode(osc.encodeMessage("/pacemaker/state", "s", "HOLD")))
check(s4[1].args[1] == "HOLD", "4 char string padding")

-- 5. nested bundle
local nested = osc.encodeBundle(1, { osc.encodeBundle(1, { beat }) })
check(#assert(osc.decode(nested)) == 1, "nested bundle")

-- 6. malformed input never throws and returns nil
local junk = { "", "abc", "/x", "/x\0\0\0,f\0\0\0\0", "#bundle\0", "#bundle\0" .. string.rep("\0", 8) .. "\0\0\0\9ab",
  "nope\0\0\0\0", ("/a\0\0,q\0\0"):rep(1) }
for i, j in ipairs(junk) do
  local ok, res = pcall(osc.decode, j)
  check(ok and (res == nil or type(res) == "table"), "junk " .. i .. " does not throw")
end
check(osc.decode("/x\0\0,f\0\0\0\0") == nil, "truncated float rejected")
check(osc.decode("/a\0\0,q\0\0") == nil, "unknown tag rejected")

-- 7. follower logic
local f = follow.new{ alpha = 0.5, deadband = 0.05, minInterval = 0.1 }
local function tempo(v) return { address = "/pacemaker/tempo", args = { v } } end
f:onMessage(tempo(120), 0)
check(f:poll(0) == 120, "first tempo applied")
f:applied(120, 0)
f:onMessage(tempo(120.02), 0.2)
check(f:poll(0.2) == nil, "inside deadband ignored")
f:onMessage(tempo(124), 0.3)               -- smoothed -> ~122
local v = f:poll(0.3)
check(v and near(v, 122.0105, 0.01), "smoothing halves the step")
f:applied(v, 0.3)
check(f:poll(0.35) == nil, "rate limit / no new data")
f:onMessage(tempo(160), 1.0)               -- jump > 12 BPM restarts smoothing
check(near(f:poll(1.0), 160), "jump restarts smoothing")
f:applied(160, 1.0)
f:onMessage({ address = "/pacemaker/confidence", args = { 0.1 } }, 2.0)
f:onMessage(tempo(170), 2.0)
check(f:poll(2.0) == nil, "low confidence gates tempo")
check(f:poll(4.0) ~= nil, "stale confidence is ignored")
f:onMessage({ address = "/pacemaker/state", args = { "HOLD" } }, 4.0)
check(f:poll(4.0) == nil, "HOLD state blocks tempo")
f:onMessage({ address = "/pacemaker/state", args = { "LOCKED" } }, 4.1)
f:onMessage(tempo(500), 4.1)
check(f:poll(4.1) ~= nil and f.smoothed < 300, "out of range tempo rejected")
f:setEnabled(false)
check(f:poll(9) == nil, "disabled follower never applies")

print(string.format("%d checks, %d failures", count, fails))
os.exit(fails == 0 and 0 or 1)
