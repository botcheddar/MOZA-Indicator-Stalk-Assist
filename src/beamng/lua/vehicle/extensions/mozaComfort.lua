-- MOZA Turn-Signal Assist: original vehicle-side implementation.
-- Only the three dedicated stalk actions call this module. Better Bindings is not modified.
local M = {}
local position, pending, comfort, ownedSide = 0, 0, 0, 0
local started, flashes, lastLit = 0, 0, false
local extraEnabled, deadline, session = false, 0, 0
local tapMs, target, maxMs = 500, 3, 6000
local suppressed, state, reason = false, 0, 'Waiting for stalk input'
local eventId, eventKind, elapsed, lastApply, lastReport = 0, 0, 0, -1000, -1000
local function now() return os.clockhp() * 1000 end
local function truth(v) return v == true or (type(v) == 'number' and v > 0) end
local function values() return electrics and electrics.values or {} end
local function hazards() return truth(values().hazard_enabled) end
local function lit(side)
  if side == -1 then return truth(values().signal_L) end
  if side == 1 then return truth(values().signal_R) end
  return false
end
local function actualSide()
  local v = values()
  if truth(v.signal_left_input) and not truth(v.signal_right_input) then return -1 end
  if truth(v.signal_right_input) and not truth(v.signal_left_input) then return 1 end
  return 0
end
local function setSide(side)
  if side == ownedSide then return false end
  if not electrics or type(electrics.set_left_signal) ~= 'function' or type(electrics.set_right_signal) ~= 'function' then
    state, reason = 6, 'Vehicle does not expose direct indicator setters'
    return false
  end
  -- These native setters reset the flash phase, so call them ONLY on state changes.
  if ownedSide == -1 then electrics.set_left_signal(false, false) end
  if ownedSide == 1 then electrics.set_right_signal(false, false) end
  if side ~= 0 then
    -- A physical stalk entry takes ownership from any earlier keyboard toggle.
    if ownedSide == 0 and actualSide() ~= 0 then
      if actualSide() == -1 then electrics.set_left_signal(false, false) else electrics.set_right_signal(false, false) end
    end
    if side == -1 then electrics.set_left_signal(true, false) else electrics.set_right_signal(true, false) end
  end
  ownedSide, lastApply = side, now()
  return true
end
local function sample()
  local side = pending ~= 0 and pending or comfort
  if side ~= 0 then
    local on = lit(side)
    if on and not lastLit then flashes = flashes + 1 end
    lastLit = on
  end
end
local function cancelExtra(why)
  comfort, pending, suppressed = 0, 0, false
  if position == 0 and not hazards() then setSide(0) end
  state, reason = position ~= 0 and 2 or 0, why
end
local function allowed()
  return extraEnabled and now() <= deadline and not hazards()
end
local function direction(side)
  if side ~= -1 and side ~= 1 then return end
  if hazards() then return end
  local repeated = comfort == side
  local opposite = comfort ~= 0 and comfort ~= side
  comfort = 0
  suppressed = repeated
  position, pending, started, elapsed = side, side, now(), 0
  local changed = setSide(side)
  lastLit = not changed and lit(side) or false
  flashes = lastLit and 1 or 0
  state = 1
  reason = repeated and 'Repeated flick: cancel at centre' or opposite and 'Opposite direction' or 'Physical stalk active'
  eventId, eventKind = eventId + 1, side == -1 and 1 or 2
end
local function centre()
  local time = now()
  sample()
  position = 0
  eventId, eventKind = eventId + 1, 3
  if pending == 0 then
    if comfort == 0 and not hazards() then setSide(0) end
    return
  end
  local side = pending
  pending, elapsed = 0, math.max(0, time - started)
  if elapsed <= tapMs and allowed() and not suppressed then
    comfort, state, reason = side, 3, 'Quick flick: complete total flashes'
    -- Keep the already active signal and phase through centre; no stop/start gap.
  else
    if not hazards() then setSide(0) end
    comfort, state = 0, suppressed and 4 or 0
    reason = suppressed and 'Repeated flick cancelled' or elapsed > tapMs and 'Held stalk released' or 'Extra flashes paused'
  end
  suppressed = false
end
local function setSession(enable, threshold, count, maximum, epoch, expires)
  local time = now()
  if type(threshold) ~= 'number' or threshold < 50 or threshold > 1500 or
     type(count) ~= 'number' or count < 1 or count > 10 or count ~= math.floor(count) or
     type(maximum) ~= 'number' or maximum < threshold or maximum > 30000 or
     type(epoch) ~= 'number' or epoch <= 0 or type(expires) ~= 'number' or
     expires < time or expires > time + 1100 then
    extraEnabled = false
    cancelExtra('Invalid or stale helper session')
    return
  end
  local fresh = epoch ~= session or (enable and not extraEnabled)
  if fresh or not enable then cancelExtra(not enable and 'Extra flashes paused' or 'Fresh helper session') end
  session, tapMs, target, maxMs = epoch, threshold, count, maximum
  extraEnabled, deadline = enable == true, expires
end
local function report(time)
  if time - lastReport < 100 then return end
  lastReport = time
  if obj and obj.queueGameEngineLua then
    local command = string.format('if extensions.mozaComfort then extensions.mozaComfort.onVehicleState(%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d) end',
      obj:getID(), position, comfort, flashes, hazards() and 1 or 0, state, eventId, eventKind,
      math.floor(elapsed), lit(-1) and 1 or 0, lit(1) and 1 or 0)
    obj:queueGameEngineLua(command)
  end
end
local function updateGFX(dt)
  local time = now()
  if extraEnabled and time > deadline then
    extraEnabled = false
    cancelExtra('Helper heartbeat expired')
  end
  if hazards() then
    -- Do not send an off command into hazard operation.
    comfort, pending, position, ownedSide, suppressed = 0, 0, 0, 0, false
    state, reason = 0, 'Hazards active'
  else
    sample()
    if pending ~= 0 and time - started > tapMs then state = 2 end
    if comfort ~= 0 then
      local v = values()
      if v.signal_L == nil or v.signal_R == nil then
        cancelExtra('Physical lamp telemetry unavailable')
      elseif time - lastApply > 80 and actualSide() ~= comfort then
        -- Respect a keyboard/controller override rather than cancelling the new input later.
        comfort, pending, ownedSide = 0, 0, 0
        state, reason = 0, 'Manual indicator override'
      elseif flashes >= target and not lastLit then
        comfort = 0
        setSide(0)
        state, reason = 5, 'Completed observed flashes'
      elseif time - started >= maxMs then
        cancelExtra('Flash watchdog expired')
      end
    end
  end
  report(time)
end
local function stop(all)
  extraEnabled = false
  if all then position = 0 end
  cancelExtra('Integration stopped')
end
local function reset()
  position, pending, comfort, ownedSide = 0, 0, 0, 0
  extraEnabled, deadline, session = false, 0, 0
  flashes, lastLit, suppressed, state = 0, false, false, 0
  reason = 'Vehicle reset: fresh stalk input required'
end
-- Exposed status is also used by deterministic tests; no game internals are copied.
local function status()
  return {position=position,pending=pending,comfort=comfort,owned=ownedSide,flashes=flashes,
    state=state,reason=reason,elapsed=elapsed,enabled=allowed()}
end
M.direction, M.centre, M.setSession = direction, centre, setSession
M.updateGFX, M.onReset, M.onExtensionUnloaded = updateGFX, reset, function() stop(true) end
M.stop, M.status = stop, status
return M

