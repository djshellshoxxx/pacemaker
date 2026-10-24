-- pm_follow.lua : tempo smoothing / deadband / gating logic, no REAPER or socket calls.
-- Pure function of (events, time) so it can be unit tested under plain lua.
--
--   local f = require("pm_follow").new{ root="/pacemaker", ... }
--   f:onMessage(msg, now)              -- msg from pm_osc.decode
--   local bpm = f:poll(now)            -- returns a bpm to apply, or nil
--   f:applied(bpm)                     -- tell it what was actually set

local M = {}
M.__index = M

local defaults = {
  root = "/pacemaker",
  alpha = 0.35,           -- exponential smoothing factor per received /tempo (1 = no smoothing)
  deadband = 0.05,        -- BPM; matches the Link default in ES-02 section 2
  minInterval = 0.10,     -- seconds between SetCurrentBPM calls
  minBpm = 30, maxBpm = 300,
  confidenceGate = 0.35,  -- ignore /tempo while last /confidence is below this
  confidenceMaxAge = 1.0, -- seconds; a stale confidence value is treated as unknown (allowed)
  jumpBpm = 12,           -- a single reading this far from the smoothed value restarts smoothing
  holdStates = { HOLD = true, IDLE = true },  -- /state values during which tempo is not applied
}

function M.new(cfg)
  local o = setmetatable({}, M)
  o.cfg = {}
  for k, v in pairs(defaults) do o.cfg[k] = v end
  for k, v in pairs(cfg or {}) do o.cfg[k] = v end
  o.smoothed = nil
  o.lastApplied = nil
  o.lastApplyTime = -1e9
  o.confidence = nil
  o.confidenceTime = -1e9
  o.state = nil
  o.enabled = true
  return o
end

function M:setEnabled(on) self.enabled = on and true or false end

function M:onMessage(msg, now)
  local a, r = msg.address, self.cfg.root
  if a == r .. "/tempo" then
    local bpm = msg.args[1]
    if type(bpm) ~= "number" or bpm ~= bpm then return end
    if bpm < self.cfg.minBpm or bpm > self.cfg.maxBpm then return end
    if self.smoothed == nil or math.abs(bpm - self.smoothed) > self.cfg.jumpBpm then
      self.smoothed = bpm
    else
      self.smoothed = self.smoothed + self.cfg.alpha * (bpm - self.smoothed)
    end
  elseif a == r .. "/confidence" then
    local c = msg.args[1]
    if type(c) == "number" then self.confidence, self.confidenceTime = c, now end
  elseif a == r .. "/state" then
    self.state = msg.args[1]
  end
end

function M:poll(now)
  if not self.enabled or self.smoothed == nil then return nil end
  local c = self.cfg
  if c.holdStates[self.state or ""] then return nil end
  if self.confidence and now - self.confidenceTime <= c.confidenceMaxAge and self.confidence < c.confidenceGate then
    return nil
  end
  if now - self.lastApplyTime < c.minInterval then return nil end
  if self.lastApplied and math.abs(self.smoothed - self.lastApplied) <= c.deadband then return nil end
  return self.smoothed
end

function M:applied(bpm, now)
  self.lastApplied = bpm
  self.lastApplyTime = now or self.lastApplyTime
end

return M
