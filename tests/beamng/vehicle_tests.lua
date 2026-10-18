local passed,failed=0,0
local function expect(ok,why) if not ok then error(why or 'assertion failed',2) end end
local function test(name,f)
  local ok,err=pcall(f)
  if ok then passed=passed+1;print('PASS '..name) else failed=failed+1;print('FAIL '..name..': '..tostring(err)) end
end
local function rig(enable,count)
  local r={time=10000,side=0,base=10000,calls={},reports={},heartbeat=enable~=false,nextHeartbeat=10200,autoCancel=false,hazard=false}
  local e={values={signal_L=0,signal_R=0,signal_left_input=0,signal_right_input=0,hazard_enabled=0}}
  local function setter(side,state,auto)
    r.calls[#r.calls+1]={side=side,on=state,time=r.time}
    r.base=r.time
    if state then r.side=side;r.autoCancel=auto==true elseif r.side==side then r.side=0;r.autoCancel=false end
    e.values.signal_left_input=r.side==-1 and 1 or 0;e.values.signal_right_input=r.side==1 and 1 or 0
  end
  e.set_left_signal=function(s,a)setter(-1,s,a)end
  e.set_right_signal=function(s,a)setter(1,s,a)end
  local env=setmetatable({os={clockhp=function()return r.time/1000 end},electrics=e,
    obj={getID=function()return 12 end,queueGameEngineLua=function(_,s)r.reports[#r.reports+1]=s end}},{__index=_G})
  local file=assert(loadfile('src/beamng/lua/vehicle/extensions/mozaComfort.lua'));setfenv(file,env)
  r.m=file();r.e=e;r.count=count or 3
  if enable~=false then r.m.setSession(true,500,r.count,6000,1,r.time+750) end
  function r:step(ms)
    local finish=self.time+ms
    repeat
      local dt=math.min(10,finish-self.time);self.time=self.time+dt
      if self.heartbeat and self.time>=self.nextHeartbeat then self.m.setSession(true,500,self.count,6000,1,self.time+750);self.nextHeartbeat=self.time+200 end
      local on=not self.noLamp and math.floor((self.time-self.base)/500)%2==0
      self.e.values.signal_L=(self.side==-1 or self.hazard) and on and 1 or 0
      self.e.values.signal_R=(self.side==1 or self.hazard) and on and 1 or 0
      self.e.values.hazard_enabled=self.hazard and 1 or 0
      self.m.updateGFX(dt/1000)
    until self.time>=finish
  end
  function r:flick(side,ms)self.m.direction(side);self:step(ms or 150);self.m.centre();self:step(0)end
  return r
end
for _,side in ipairs({-1,1}) do
 test('Three total physical flashes '..side,function()
   local r=rig();r:flick(side,150);expect(r.side==side,'signal did not continue')
   r:step(2340);expect(r.side==side,'third lit phase cut short');r:step(20)
   expect(r.side==0 and r.m.status().flashes==3,'wrong total flash count')
 end)
 test('Same-side repeat cancellation '..side,function()
   local r=rig();r:flick(side);r:step(250);r.m.direction(side);expect(r.m.status().comfort==0,'comfort retained')
   r:step(150);r.m.centre();expect(r.side==0 and r.m.status().reason=='Repeated flick cancelled','repeat restarted');r:step(4000);expect(r.side==0,'ghost sequence')
 end)
 test('Opposite switching '..side,function()
   local r=rig();r:flick(side);r:step(300);r.m.direction(-side);expect(r.side==-side,'opposite not immediate')
   r:step(150);r.m.centre();r:step(2400);expect(r.side==0 and r.m.status().flashes==3,'opposite count wrong')
 end)
 test('Full detent preempts active comfort '..side,function()
   local r=rig();r:flick(-side);r:step(200);r.m.direction(side);r:step(8000)
   expect(r.side==side and r.m.status().comfort==0,'timer owned full detent');r.m.centre();expect(r.side==0,'full return did not cancel')
 end)
end
for _,ms in ipairs({47,150,400,500,600,2406}) do test('Separate centre event after '..ms..'ms',function()
 local r=rig();r:flick(-1,ms);expect(r.side==(ms<=500 and -1 or 0),'threshold wrong')
end)end
test('Holding the soft position blinks until physical centre',function()
 local r=rig();r.m.direction(-1);r:step(5000);expect(r.side==-1,'held soft position stopped');r.m.centre();expect(r.side==0,'soft release not immediate')
end)
test('Centre without direction ignored',function()local r=rig();r.m.centre();expect(#r.calls==0,'orphan centre injected')end)
test('Duplicate centre never restarts',function()local r=rig();r:flick(-1);local n=#r.calls;r.m.centre();expect(#r.calls==n and r.side==-1,'duplicate centre changed phase')end)
test('No setter spam or phase reset at quick centre',function()local r=rig();r:flick(-1);r:step(1000);expect(#r.calls==1,'setter repeated while held');r:step(1500);expect(#r.calls==2 and r.side==0,'wrong edge output count')end)
test('Normal stalk works without the desktop helper',function()local r=rig(false);r.m.direction(1);r:step(3000);expect(r.side==1,'normal state depends on helper');r.m.centre();expect(r.side==0,'normal centre broken');r:flick(-1);expect(r.side==0,'extra flashes without helper')end)
test('Helper expiry releases only comfort ownership',function()local r=rig();r:flick(-1);r.heartbeat=false;r:step(800);expect(r.side==0,'expired comfort held')end)
test('Helper expiry does not cancel physically latched normal signal',function()local r=rig();r.m.direction(-1);r.heartbeat=false;r:step(5000);expect(r.side==-1,'helper expiry cancelled full detent');r.m.centre();expect(r.side==0,'normal centre after expiry failed')end)
test('Enable during normal hold does not invent a quick flick',function()local r=rig(false);r.m.direction(1);r:step(100);r.m.setSession(true,500,3,6000,2,r.time+750);r.m.centre();expect(r.side==0,'old candidate resurrected')end)
test('Keyboard override is preserved',function()local r=rig();r:flick(-1);r:step(150);r.side=1;r.e.values.signal_left_input=0;r.e.values.signal_right_input=1;r:step(100);expect(r.side==1 and r.m.status().comfort==0,'new keyboard signal cancelled');r:step(5000);expect(r.side==1,'delayed old off command')end)
test('Native automatic steering cancel is disabled for latched stalk',function()local r=rig();r.side=1;r.autoCancel=true;r.e.values.signal_right_input=1;r.m.direction(-1);expect(r.side==-1 and not r.autoCancel,'old automatic cancel leaked into physical stalk')end)
test('Hazards preempt comfort without off commands',function()local r=rig();r:flick(-1);local n=#r.calls;r.hazard=true;r:step(200);expect(#r.calls==n and r.m.status().comfort==0,'hazards interfered with')end)
test('Expired queued configuration rejected',function()local r=rig(false);r.m.setSession(true,500,3,6000,99,r.time-1);r:flick(-1);expect(r.side==0,'stale enable replayed')end)
test('Invalid flash target rejected',function()local r=rig(false);r.m.setSession(true,500,100,6000,1,r.time+750);r:flick(-1);expect(r.side==0,'invalid count enabled')end)
test('Vehicle reset has no stale action',function()local r=rig();r:flick(-1);r.m.onReset();r.side=0;r:step(1000);expect(r.side==0 and r.m.status().comfort==0,'reset replayed')end)
test('Custom per-game count supported',function()local r=rig(true,5);r:flick(1);r:step(3000);expect(r.side==1,'three-flash constant hardcoded');r:step(1500);expect(r.side==0 and r.m.status().flashes==5,'five count wrong')end)
test('6000ms watchdog bounds missing flashes',function()local r=rig();r:flick(-1);r.noLamp=true;r:step(6000);expect(r.side==0 and r.m.status().reason=='Flash watchdog expired','watchdog failed')end)
test('Stalk direction during hazards does not disable hazards',function()local r=rig();r.hazard=true;r:step(10);local n=#r.calls;r.m.direction(-1);r.m.centre();expect(#r.calls==n,'stalk disabled hazards')end)
print(string.format('RESULT: %d passed, %d failed',passed,failed));os.exit(failed==0 and 0 or 1)
