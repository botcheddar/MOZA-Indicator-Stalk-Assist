local passed,failed=0,0
local function expect(b,s)if not b then error(s or 'assertion failed',2)end end
local function test(n,f)local ok,e=pcall(f);if ok then passed=passed+1;print('PASS '..n)else failed=failed+1;print('FAIL '..n..': '..tostring(e))end end
local function rig()
 local r={time=10000,received={},sent={},commands={},id=12}
 local function vehicle(id)return{getID=function()return id end,queueLuaCommand=function(_,s)r.commands[#r.commands+1]={id=id,code=s}end}end
 r.vehicle=vehicle(12);r.other=vehicle(13)
 local udp={settimeout=function()end,setsockname=function()return true end,close=function()r.closed=true end,
 receivefrom=function()local p=table.remove(r.received,1);if p then return unpack(p)end end,
 sendto=function(_,s,h,p)r.sent[#r.sent+1]={s,h,p};return #s end}
 local env=setmetatable({require=function(n)expect(n=='socket');return {udp=function()return udp end}end,
 os={clockhp=function()return r.time/1000 end},be={getPlayerVehicle=function()return r.vehicle end},log=function()end},{__index=_G})
 local f=assert(loadfile('src/beamng/lua/ge/extensions/mozaComfort.lua'));setfenv(f,env);r.m=f();r.m.onExtensionLoaded()
 function r:step(ms)self.time=self.time+(ms or 0);self.m.onUpdate(0.01,0.01)end
 function r:reply(s,host,port)self.received[#self.received+1]={s,host or '127.0.0.1',port or 40518};self:step(1)end
 function r:valid(seq)return 'MZC2 '..(seq or 1)..' 55 1 500 3 6000'end
 function r:sessionCount()local n=0;for _,c in ipairs(self.commands)do if c.code:find('setSession',1,true)then n=n+1 end end;return n end
 function r:last()return self.commands[#self.commands].code end
 r:step();return r
end
test('Telemetry protocol matches desktop parser',function()local r=rig();expect(r.sent[1][1]=='MZB2 1 12 0 0 0 0 0 0 0 0 0 0','wire mismatch');expect(r.sent[1][2]=='127.0.0.1' and r.sent[1][3]==40518,'nonlocal peer')end)
test('Recent numeric reply queues bounded vehicle session',function()local r=rig();r:reply(r:valid());expect(r:sessionCount()==1 and r:last():find('setSession(true,500,3,6000,55,',1,true),'no session')end)
test('Unknown request is rejected',function()local r=rig();r:reply(r:valid(99));expect(r:sessionCount()==0)end)
test('Replayed response rejected',function()local r=rig();r:reply(r:valid());r:reply(r:valid());expect(r:sessionCount()==1)end)
test('Old response rejected after 300ms',function()local r=rig();r.time=r.time+301;r:reply(r:valid());expect(r:sessionCount()==0)end)
test('Wrong source host and port rejected',function()local r=rig();r:reply(r:valid(),'192.0.2.1');r:reply(r:valid(),'127.0.0.1',123);expect(r:sessionCount()==0)end)
test('Executable and oversized packets rejected',function()local r=rig();r:reply(r:valid()..';bad()');r:reply(string.rep('1',257));expect(r:sessionCount()==0)end)
test('Out of range configuration rejected',function()local r=rig();r:reply('MZC2 1 55 1 10 100 6000');expect(r:sessionCount()==0)end)
test('Pause suppresses extra flashes despite enabled helper',function()local r=rig();r.m.onPhysicsPaused();r:reply(r:valid());expect(r:last():find('setSession(false,',1,true),'paused enabled')end)
test('Unpause requires fresh response',function()local r=rig();r.m.onPhysicsPaused();r.m.onPhysicsUnpaused();r:reply(r:valid());expect(r:last():find('setSession(true,',1,true),'unpause blocked')end)
test('Missing helper stops extras without cancelling normal hold',function()local r=rig();r:step(1000);expect(r:last():find('stop(false)',1,true),'wrong stop scope')end)
test('Vehicle switch cleans previous ownership',function()local r=rig();r.vehicle=r.other;r:step(100);local found=false;for _,c in ipairs(r.commands)do if c.id==12 and c.code:find('stop(true)',1,true)then found=true end end;expect(found)end)
test('Vehicle status rejects reports from another car',function()local r=rig();r.m.onVehicleState(999,-1,-1,2,0,3,9,1,150,1,0);r:step(100);expect(r.sent[2][1]=='MZB2 2 12 0 0 0 0 0 0 0 0 0 0')end)
test('Vehicle status carries observed flashes',function()local r=rig();r.m.onVehicleState(12,-1,-1,2,0,3,9,1,150,1,0);r:step(100);expect(r.sent[2][1]=='MZB2 2 12 -1 -1 2 0 3 9 1 150 1 0')end)
test('Unload releases vehicle and socket',function()local r=rig();r.m.onExtensionUnloaded();expect(r.closed and r:last():find('stop(true)',1,true))end)
test('No player vehicle still reports idle safely',function()local r=rig();r.vehicle=nil;r:step(100);expect(r.sent[2][1]=='MZB2 2 0 0 0 0 0 0 0 0 0 0 0')end)
print(string.format('RESULT: %d passed, %d failed',passed,failed));os.exit(failed==0 and 0 or 1)
