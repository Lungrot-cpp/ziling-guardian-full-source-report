// Browser plugin not available; Playwright fixtures test controls, not real recognition.
const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path'),http=require('node:http'),os=require('node:os');
const {chromium}=require('C:/Users/xcy/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright');
const net=path.resolve(__dirname,'../software/src/net'),out=path.join(os.tmpdir(),'fpga-infrared-ui-20261102');fs.mkdirSync(out,{recursive:true});
let enabled=1,active=1,calibrated=0,reject=false,offline=false;const posts=[];
const server=http.createServer((req,res)=>{
 const name=req.url.split('?')[0],json=value=>{res.setHeader('Content-Type','application/json');res.end(JSON.stringify(value));};
 if(name==='/api/infrared' && req.method==='POST'){
  let body='';req.on('data',b=>body+=b);req.on('end',()=>{posts.push(body);if(reject){res.statusCode=409;return json({ok:false});}enabled=body==='enabled=1'?1:0;json({ok:true});});return;
 }
 if(name==='/api/status'){
  if(offline){res.statusCode=503;return json({});}
  return json({server:'riscv-lwip',uptime_ms:10000,capture_us:9900000,period_ms:500,alarm_latched:0,
   infrared_assist:{enabled,active,calibrated},tracks:[{id:1,state:0,detector:800,rule:0,weighted:0,quality:active?1:0,person_confirmed:active?0:1}],
   sensors:{env_valid:1,ambient:2400,humidity:5000,thermal_valid:0,heat:[],thermal_frames:0}});
 }
 if(name==='/api/tracking')return json({mode:'single',active_mode:'single',tracks:[]});
 if(name==='/api/events')return json({events:[]});
 if(name==='/api/report')return json({});
 const file=path.resolve(net,name==='/'?'board.html':name.slice(1));
 if(!file.startsWith(net+path.sep)||!fs.existsSync(file)){res.statusCode=404;return res.end();}
 res.setHeader('Content-Type',name.endsWith('.js')?'application/javascript':name.endsWith('.css')?'text/css':name.endsWith('.png')?'image/png':'text/html;charset=utf-8');res.end(fs.readFileSync(file));
});
(async()=>{
 await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));const base=`http://127.0.0.1:${server.address().port}`;
 const browser=await chromium.launch({channel:'msedge',headless:true});
 try{
  const page=await browser.newPage(),errors=[];page.on('pageerror',e=>errors.push(e.message));
  await page.route('http://192.168.0.3:8093/**',r=>r.fulfill({contentType:'application/json',body:JSON.stringify({source:'v6-ethernet',connected:false,fps:0})}));
  await page.route('http://192.168.0.3:8092/**',r=>r.fulfill({contentType:'application/json',body:'{"events":[],"count":0}'}));
  for(const [name,width,height] of [['desktop',1366,1000],['mobile',390,844]]){
   enabled=active=1;await page.setViewportSize({width,height});await page.goto(base+'/?snapshot=1');
   const toggle=page.getByRole('switch',{name:'红外辅助识别'});
   await page.locator('#infrared-status').filter({hasText:'待 RGB／红外标定'}).waitFor();
   assert.match(await page.title(),/现场监测/);assert.equal(new URL(page.url()).origin,base);
   assert.equal(await toggle.getAttribute('aria-checked'),'true');
   assert.match(await page.locator('#people').textContent(),/0 人已确认/);
   assert.equal(await page.locator('.track.unconfirmed').count(),1);
   await toggle.focus();await page.keyboard.press('Space');
   await page.locator('#infrared-status').filter({hasText:'等待板端'}).waitFor();
   assert.equal(posts.at(-1),'enabled=0');assert(await toggle.isDisabled());
   await page.evaluate(()=>loadStatus());assert(await toggle.isDisabled(),'pending until actual board processing');
   active=0;await page.evaluate(()=>loadStatus());
   assert.equal(await toggle.getAttribute('aria-checked'),'false');assert.match(await page.locator('#infrared-status').textContent(),/仅 RGB/);
   assert.equal(await page.locator('#people').textContent(),'1 人');assert.equal(await page.locator('.track.normal').count(),1);
   await toggle.click();await page.locator('#infrared-status').filter({hasText:'等待板端'}).waitFor();
   assert.equal(posts.at(-1),'enabled=1');active=1;await page.evaluate(()=>loadStatus());
   await toggle.scrollIntoViewIfNeeded();assert(await toggle.isVisible());
   assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth));
   await page.screenshot({path:path.join(out,`${name}-fixture.png`),fullPage:true});
  }
  reject=true;await page.getByRole('switch').click();await page.locator('#infrared-status').filter({hasText:'切换失败'}).waitFor();
  assert.equal(await page.getByRole('switch').getAttribute('aria-checked'),'true');
  offline=true;await page.evaluate(()=>loadStatus());assert(await page.getByRole('switch').isDisabled());
  assert.match(await page.locator('#infrared-status').textContent(),/状态不可用/);
  offline=false;reject=false;calibrated=1;await page.evaluate(()=>loadStatus());assert.match(await page.locator('#infrared-status').textContent(),/按目标区域/);
  assert.deepEqual(errors,[]);console.log('INFRARED_UI_PASS: desktop/mobile, default on, keyboard, actual-state wait, both POST values, rejected/offline states, no JS errors/overflow; fixture only. '+out);
 }finally{await browser.close();}
})().catch(e=>{console.error(e);process.exitCode=1;}).finally(()=>server.close());
