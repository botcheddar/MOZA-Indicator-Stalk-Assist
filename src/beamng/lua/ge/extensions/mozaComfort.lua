-- Local-only bridge between BeamNG and the portable Turn-Signal Assist app.
local M = {}
local socket = require('socket')
local udp, vehicle, lastVehicle, sequence, lastAccepted = nil, nil, nil, 0, 0
local sentAt, lastSend, lastReply = {}, -1000, -1000
local paused, latest = false, nil
local PORT = 40518
local function now() return os.clockhp() * 1000 end
local function player()
  return be and be:getPlayerVehicle(0) or nil
end
local function queue(target, code)
  if target and target.queueLuaCommand then target:queueLuaCommand(code) end
end
local function stopVehicle(target, all)
  queue(target, 'if extensions.mozaComfort then extensions.mozaComfort.stop('..tostring(all == true)..') end')
end
local function onVehicleState(id, position, comfort, flashes, hazard, state, event, kind, elapsed, left, right)
  local current = player()
  if current and current:getID() == id then
    latest = {position,comfort,flashes,hazard,state,event,kind,elapsed,left,right}
  end
end
local function connect()
  if udp then return true end
  udp = socket.udp()
  if not udp then return false end
  udp:settimeout(0)
  local ok = udp:setsockname('127.0.0.1',0)
  if not ok then udp:close();udp=nil;return false end
  return true
end
local function onUpdate(dtReal,dtSim)
  local time = now()
  if simTimeAuthority and simTimeAuthority.getPause then paused=simTimeAuthority.getPause() end
  vehicle = player()
  if vehicle ~= lastVehicle then
    stopVehicle(lastVehicle,true)
    lastVehicle, latest, lastReply = vehicle, nil, -1000
  end
  if not connect() then return end
  -- Drain a bounded number of packets without blocking the game's main thread.
  for i=1,16 do
    local packet,host,port = udp:receivefrom()
    if not packet then break end
    if host == '127.0.0.1' and port == PORT and #packet <= 256 then
      local seq,epoch,enable,threshold,count,maximum = packet:match('^MZC2 (%d+) (%d+) ([01]) (%d+) (%d+) (%d+)$')
      seq,epoch,enable,threshold,count,maximum=tonumber(seq),tonumber(epoch),tonumber(enable),tonumber(threshold),tonumber(count),tonumber(maximum)
      if seq and sentAt[seq] and time-sentAt[seq] <= 300 and seq>lastAccepted and
         threshold>=50 and threshold<=1500 and count>=1 and count<=10 and maximum<=30000 and epoch>0 then
        lastAccepted,lastReply=seq,time
        if vehicle then
          local allow=enable==1 and not paused
          queue(vehicle,string.format("extensions.use('mozaComfort').setSession(%s,%d,%d,%d,%.0f,%.3f)",
            tostring(allow),threshold,count,maximum,epoch,time+750))
        end
      end
    end
  end
  if time-lastSend>=100 then
    lastSend,sequence=time,sequence+1
    sentAt[sequence]=time;sentAt[sequence-8]=nil
    local id=vehicle and vehicle:getID() or 0
    local s=latest or {0,0,0,0,0,0,0,0,0,0}
    udp:sendto(string.format('MZB2 %d %d %d %d %d %d %d %d %d %d %d %d',sequence,id,
      s[1],s[2],s[3],s[4],s[5],s[6],s[7],s[8],s[9],s[10]),'127.0.0.1',PORT)
    if vehicle and time-lastReply>750 then stopVehicle(vehicle,false) end
  end
end
local function onExtensionLoaded()
  connect()
  log('I','mozaComfort','MOZA Turn-Signal Assist local bridge loaded; standalone normal stalk control remains available')
end
local function onPhysicsPaused() paused=true;stopVehicle(player(),false) end
local function onPhysicsUnpaused() paused=false;lastReply=-1000 end
local function shutdown()
  stopVehicle(lastVehicle,true)
  if udp then udp:close();udp=nil end
  latest,lastVehicle=nil,nil
end
M.onExtensionLoaded,M.onUpdate=onExtensionLoaded,onUpdate
M.onVehicleState=onVehicleState
M.onPhysicsPaused,M.onPhysicsUnpaused=onPhysicsPaused,onPhysicsUnpaused
M.onClientEndMission,M.onExtensionUnloaded=shutdown,shutdown
return M

