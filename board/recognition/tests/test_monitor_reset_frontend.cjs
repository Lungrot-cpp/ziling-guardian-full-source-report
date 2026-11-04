// Fixtures verify the real page controls, not physical fall detection.
const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path'),http=require('node:http'),os=require('node:os');
const {chromium}=require('C:/Users/xcy/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright');
const net=path.resolve(__dirname,'../software/src/net'),out=path.join(os.tmpdir(),'fpga-monitor-reset-ui-20261102');fs.mkdirSync(out,{recursive:true});
let requested=0,completed=0,alarm=1,reject=false,offline=false,posts=0;
const server=http.createServer((req,res)=>{
 const name=req.url.split('?')[0],json=value=>{res.setHeader('Content-Type','application/json');res.end(JSON.stringify(value));};
 if(name==='/api/monitor/reset' && req.method==='POST'){
  let body='';req.on('data',b=>body+=b);req.on('end',()=>{posts++;assert.equal(body,'');if(reject){res.statusCode=409;return json({ok:false});}json({ok:true,reset_id:++requested});});return;
 }
 if(name==='/api/status'){
  if(offline){res.statusCode=503;return json({});}
  return json({server:'riscv-lwip',uptime_ms:10000,capture_us:9900000,period_ms:500,alarm_latched:alarm,
   monitor_reset:{requested,completed},infrared_assist:{enabled:0,active:0,calibrated:0},tracks:[],
   sensors:{env_valid:1,ambient:2400,humidity:5000,thermal_valid:0,heat:[],thermal_frames:0}});
 }
 if(name==='/api/tracking')return json({mode:'single',active_mode:'single',tracks:[]});
 if(name==='/api/events')return json({events:alarm?[{id:1,track:1,state:2,rule:900,uptime_ms:1000}]:[],volatile:true});
 if(name==='/api/report')return json({});
 const file=path.resolve(net,name==='/'?'board.html':name.slice(1));
 if(!file.startsWith(net+path.sep)||!fs.existsSync(file)){res.statusCode=404;return res.end();}
 res.setHeader('Content-Type',name.endsWith('.js')?'application/javascript':name.endsWith('.css')?'text/css':name.endsWith('.png')?'image/png':'text/html;charset=utf-8');res.end(fs.readFileSync(file));
});
(async()=>{
 await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));const base=`http://127.0.0.1:${server.address().port}`;
 const browser=await chromium.launch({channel:'msedge',headless:true});
 try {
  const page=await browser.newPage(),errors=[];page.on('pageerror',e=>errors.push(e.message));
  await page.route('http://192.168.0.3:8093/**',r=>r.fulfill({contentType:'application/json',body:'{"source":"v6-ethernet","connected":false,"fps":0}'}));
  await page.route('http://192.168.0.3:8092/**',r=>r.fulfill({contentType:'application/json',body:'{"events":[],"count":0}'}));
  for(const [name,width,height] of [['desktop',1366,1000],['mobile',390,844]]) {
   requested=completed=0;alarm=1;await page.setViewportSize({width,height});await page.goto(base+'/?snapshot=1');
   const button=page.getByRole('button',{name:'重新测试',exact:true});
   await page.locator('#overall-label').filter({hasText:'确认跌倒'}).waitFor();await button.waitFor();
   await button.focus();await page.keyboard.press('Space');
   await page.locator('#reset-monitor-status').filter({hasText:'等待板端'}).waitFor();
   assert(await page.locator('#reset-monitor').isDisabled());assert.equal(requested,1);
   assert.equal(await page.locator('#overall-label').textContent(),'确认跌倒','POST acceptance alone must not fake normal');
   completed=requested;alarm=0;await page.evaluate(()=>loadStatus());
   await page.locator('#reset-monitor-status').filter({hasText:'已重置'}).waitFor();
   assert.equal(await page.locator('#overall-label').textContent(),'状态正常');assert(await button.isEnabled());
   // A fresh fall after reset must remain visible and allow another round.
   alarm=1;await page.evaluate(()=>loadStatus());assert.equal(await page.locator('#overall-label').textContent(),'确认跌倒');
   await button.click();await page.locator('#reset-monitor-status').filter({hasText:'等待板端'}).waitFor();
   assert.equal(requested,2);completed=2;alarm=0;await page.evaluate(()=>loadStatus());
   assert(await button.isEnabled());assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth));
   await page.screenshot({path:path.join(out,name+'.png'),fullPage:true});
  }
  reject=true;alarm=1;await page.evaluate(()=>loadStatus());await page.getByRole('button',{name:'重新测试',exact:true}).click();
  await page.locator('#reset-monitor-status').filter({hasText:'请求未确认'}).waitFor();assert(await page.locator('#reset-monitor').isEnabled());
  assert.equal(await page.locator('#overall-label').textContent(),'确认跌倒');
  reject=false;await page.locator('#reset-monitor').click();await page.locator('#reset-monitor-status').filter({hasText:'等待板端'}).waitFor();
  await page.evaluate(()=>{resetDeadline=0;return loadStatus();});
  assert.match(await page.locator('#reset-monitor-status').textContent(),/请重试/);assert(await page.locator('#reset-monitor').isEnabled());
  offline=true;await page.evaluate(()=>loadStatus());assert(await page.locator('#reset-monitor').isDisabled());
  offline=false;await page.evaluate(()=>loadStatus());assert(await page.locator('#reset-monitor').isEnabled());
  assert.deepEqual(errors,[]);assert.equal(posts,6);
  console.log('MONITOR_RESET_UI_PASS: desktop/mobile, keyboard, real-state completion wait, second fall and repeat reset, rejected/timeout/offline recovery, no JS errors/overflow; fixtures only. '+out);
 }finally{await browser.close();}
})().catch(e=>{console.error(e);process.exitCode=1;}).finally(()=>server.close());
