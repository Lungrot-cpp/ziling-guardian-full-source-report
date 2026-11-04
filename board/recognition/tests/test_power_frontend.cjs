const assert=require('node:assert/strict'),fs=require('node:fs'),vm=require('node:vm');
const app=fs.readFileSync(__dirname+'/../software/src/net/app.js','utf8');
const nodes={},posts=[];
const context={$:id=>nodes[id]||(nodes[id]={textContent:'',disabled:false}),AbortSignal,
 fetch:async(path,options)=>{posts.push({path,...options});return {ok:true,json:async()=>({ok:true})};},loadStatus:async()=>{}};
vm.createContext(context);
vm.runInContext(app.slice(app.indexOf('let lastPowerSample='),app.indexOf('function renderStatus(data)')),context);
const sample=(sample_ms,inferences,inference_us,mode='active',automatic=1)=>({sample_ms,inferences,inference_us,mode,automatic,wake_reason:'motion',event_blocked:0,monitor_ok:1,empty_ms:20000,residence_ms:[10000,2000,0]});
context.renderPower(null);assert(nodes['#power-toggle'].disabled);
context.renderPower(sample(1000,10,1000000));assert.match(nodes['#power-state'].textContent,/10.0 秒/);
assert.match(nodes['#monitor-power-detail'].textContent,/连续无人.*10.0 秒/);
context.renderPower(sample(6000,20,2000000));assert.match(nodes['#power-metrics'].textContent,/2.00 次\/秒.*20.0%/);
context.renderPower(sample(11000,22,2200000,'watch'));assert.match(nodes['#power-metrics'].textContent,/0.40 次\/秒.*4.0%/);
assert.match(nodes['#power-state'].textContent,/轻量监视.*检测到运动/);
assert.equal(nodes['#monitor-power-mode'].textContent,'低功耗监视');
assert.match(nodes['#monitor-power-detail'].textContent,/HDMI 画面调暗.*2 帧.*每 2 秒/);
context.renderPower({...sample(11500,22,2200000),event_blocked:1});assert.match(nodes['#monitor-power-detail'].textContent,/告警／救援/);
context.renderPower({...sample(11600,22,2200000),monitor_ok:0});assert.match(nodes['#monitor-power-detail'].textContent,/传感器状态异常/);
context.renderPower(sample(11700,22,2200000,'waking'));assert.match(nodes['#monitor-power-detail'].textContent,/已唤醒.*10 帧/);
context.renderPower(sample(11800,22,2200000,'watch'));
(async()=>{
 await nodes['#power-toggle'].onclick();assert.equal(posts.at(-1).body,'mode=continuous');
 context.renderPower(sample(12000,23,2300000,'active',0));await nodes['#power-toggle'].onclick();assert.equal(posts.at(-1).body,'mode=auto');
 context.renderPower(null);assert.match(nodes['#power-metrics'].textContent,/等待采样/);assert(nodes['#power-toggle'].disabled);
 assert.match(nodes['#monitor-power-mode'].textContent,/数据不可用/);
 console.log('POWER_FRONTEND_PASS: measured counter rates, inference time share, state, offline reset and both mode switches');
})().catch(e=>{console.error(e);process.exitCode=1});
