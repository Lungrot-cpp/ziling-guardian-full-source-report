// Synthetic HTTP data verifies the UI only; it is not board or power evidence.
const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path'),http=require('node:http');
const {chromium}=require('C:/Users/xcy/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright');
const net=path.join(__dirname,'../software/src/net'),out=path.join(__dirname,'../../validation');
let automatic=1,mode='active',ms=10000,count=20,busy=2000000;
function power(){return {automatic,mode,sample_ms:ms,inferences:count,inference_us:busy,wake_reason:'motion',event_blocked:0,monitor_ok:1,empty_ms:20000,residence_ms:[10000,12000,0]};}
const server=http.createServer((req,res)=>{
 const url=new URL(req.url,'http://localhost');
 if(req.method==='POST' && url.pathname==='/api/power'){
  let body='';req.on('data',b=>body+=b);req.on('end',()=>{automatic=body==='mode=auto'?1:0;mode='active';res.setHeader('Content-Type','application/json');res.end('{"ok":true}');});return;
 }
 const values={'/api/status':{server:'riscv-lwip',uptime_ms:ms,capture_us:ms*1000,period_ms:mode==='watch'?2000:500,tracks:[],alarm_latched:0,rx_bad:0,rx_dropped:0,power:power(),sensors:{env_valid:1,ambient:2500,humidity:5000,thermal_valid:1,heat:Array(768).fill(2500),age_us:100000,thermal_frames:50}},'/api/events':{events:[]}};
 if(values[url.pathname]){res.setHeader('Content-Type','application/json');res.end(JSON.stringify(values[url.pathname]));return;}
 const name=url.pathname==='/'?'board.html':url.pathname.slice(1),file=path.resolve(net,name);
 if(!file.startsWith(path.resolve(net)+path.sep)||!fs.existsSync(file)){res.writeHead(404);res.end();return;}
 res.setHeader('Content-Type',name.endsWith('.js')?'application/javascript':name.endsWith('.css')?'text/css':name.endsWith('.png')?'image/png':'text/html; charset=utf-8');res.end(fs.readFileSync(file));
});
(async()=>{
 await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
 const browser=await chromium.launch({channel:'msedge',headless:true});
 try{
  const page=await browser.newPage({viewport:{width:1366,height:1000}}),errors=[];page.on('pageerror',e=>errors.push(e.message));
  await page.goto(`http://127.0.0.1:${server.address().port}/?snapshot=1#device`);
  await page.locator('#power-state').filter({hasText:'正常检测'}).waitFor();
  ms+=5000;count+=10;busy+=1000000;await page.evaluate(()=>loadStatus());
  assert.match(await page.locator('#power-metrics').innerText(),/2.00 次\/秒.*20.0%/);
  mode='watch';ms+=5000;count+=2;busy+=200000;await page.evaluate(()=>loadStatus());
  assert.match(await page.locator('#power-metrics').innerText(),/0.40 次\/秒.*4.0%/);
  assert.equal(await page.locator('#monitor-power-mode').textContent(),'低功耗监视');
  await page.locator('[data-nav=monitor]').click();
  await page.locator('#monitor-power-mode').waitFor({state:'visible'});
  for(const [name,width,height] of [['desktop',1366,1000],['mobile',390,844]]){
   await page.setViewportSize({width,height});
   assert(await page.locator('#monitor-power-mode').isVisible());
   assert.match(await page.locator('#monitor-power-detail').innerText(),/HDMI 画面调暗.*2 帧/);
   assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),'monitor horizontal overflow');
   await page.screenshot({path:path.join(out,`power-monitor-${name}-fixture.png`),fullPage:true});
  }
  mode='waking';await page.evaluate(()=>loadStatus());
  assert.match(await page.locator('#monitor-power-detail').innerText(),/已唤醒.*10 帧/);
  await page.locator('[data-nav=device]').click();
  await page.screenshot({path:path.join(out,'power-ui-desktop.png'),fullPage:true});
  await page.locator('#power-toggle').click();await page.locator('#power-state').filter({hasText:'连续检测对照'}).waitFor();
  await page.locator('#power-toggle').click();await page.locator('#power-state').filter({hasText:'自动管理'}).waitFor();
  await page.setViewportSize({width:390,height:844});await page.screenshot({path:path.join(out,'power-ui-mobile.png'),fullPage:true});
  assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),'horizontal overflow');
  assert.deepEqual(errors,[]);console.log('POWER_BROWSER_PASS: desktop/mobile, measured counters, both switches, no page errors or horizontal overflow; synthetic data only');
 }finally{await browser.close();}
})().catch(e=>{console.error(e);process.exitCode=1;}).finally(()=>server.close());
