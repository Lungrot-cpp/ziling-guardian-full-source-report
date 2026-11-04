// Browser plugin not available; installed Playwright checks fixtures, never hardware actions.
const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path'),http=require('node:http'),os=require('node:os');
const {chromium}=require('C:/Users/xcy/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright');
const net=path.resolve(__dirname,'../software/src/net'),evidence=path.join(os.tmpdir(),'fpga-unified-web-20261101');fs.mkdirSync(evidence,{recursive:true});
const session='00112233445566778899aabbccddeeff',posts=[],calls={};let mode='single',offline=false,archiveOffline=false;
const care={hardware:true,calibrated:false,valid:false,age_ms:0,room:-1,wall:-1,risk:0,armed:false,door_state:0,countdown_ms:0,alarm:false,
 rooms:[{rect:[0,0,.5,.5]},{rect:[.5,0,1,.5]},{rect:[0,.5,.5,1]},{rect:[.5,.5,1,1]}],trail_ages:Array(256).fill(-1),config:{brightness:3,leds_per_room:7,rotation:0,matrix_rgb:0,strip_rgb:0,serpentine:1,neutral_us:1500,open_us:1650,close_us:1350,run_ms:400}};
const status={server:'riscv-lwip',uptime_ms:10000,capture_us:9900000,period_ms:500,tracks:[],alarm_latched:0,rx_bad:0,rx_dropped:0,
 sensors:{env_valid:1,ambient:2500,humidity:5000,thermal_valid:1,heat:Array(768).fill(2500),age_us:100000,thermal_frames:50}};
const event={id:1,track:4,state:2,frame:20,uptime_ms:9000,detector:800,rule:600,score:600,weighted:0,x0:20,y0:20,x1:400,y1:700,session,received_utc:'2026-11-01T12:00:00Z'};
const server=http.createServer((req,res)=>{
 const name=req.url.split('?')[0];calls[name]=(calls[name]||0)+1;
 const json=data=>{res.setHeader('Content-Type','application/json');res.end(JSON.stringify(data))};
 if(req.method==='POST'){let body='';req.on('data',chunk=>body+=chunk);req.on('end',()=>{posts.push({name,body});if(name==='/api/tracking')mode=new URLSearchParams(body).get('mode');json({ok:true})});return;}
 if(name==='/api/tracking'){if(offline){res.writeHead(503);return res.end('{}')}return json({mode,active_mode:mode,age_ms:100,tracks:[{id:4,state:0,box:[20,20,400,700],trail:[[100,200],[110,220]]}]})}
 if(name==='/api/status')return json(status);
 if(name==='/api/events')return json({volatile:true,events:[event]});
 if(name==='/api/report')return json({session});
 if(name==='/api/care')return json(care);
 if(name==='/api/replay')return json({ready:false,buffered:0,count:0,frames:[]});
 if(name==='/frame.bmp'){res.setHeader('Content-Type','image/jpeg');return res.end(fs.readFileSync(path.join(net,'media/demo-frame.jpg')))}
 const file=path.join(net,name==='/'?'board.html':name);
 if(!fs.existsSync(file)){res.writeHead(404);return res.end()}
 res.setHeader('Content-Type',name.endsWith('.js')?'application/javascript':name.endsWith('.css')?'text/css':'text/html;charset=utf-8');res.end(fs.readFileSync(file));
});
(async()=>{
 await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));const base=`http://127.0.0.1:${server.address().port}`;
 const browser=await chromium.launch({channel:'msedge',headless:true});
 try{
  const page=await browser.newPage(),errors=[];page.on('pageerror',error=>errors.push(error.message));
  await page.route('http://192.168.0.3:8093/**',route=>route.fulfill({contentType:'application/json',body:JSON.stringify({source:'v6-ethernet',connected:false,fps:0})}));
  await page.route('http://192.168.0.3:8092/**',route=>{
   if(archiveOffline)return route.abort();
   if(route.request().url().includes('/image/'))return route.fulfill({contentType:'image/jpeg',body:fs.readFileSync(path.join(net,'media/demo-frame.jpg'))});
   return route.fulfill({contentType:'application/json',body:JSON.stringify({connected:true,count:1,duplicates:0,events:[event]})});
  });
  for(const [name,width,height] of [['desktop',1366,950],['mobile',390,844]]){
   await page.setViewportSize({width,height});await page.goto(base+'/');
   assert.equal(await page.locator('#page-description,#overall-help,#video-note,#media-disclaimer,#path-note,#chart-note,.sound-controls small,.page-view>p.control-note,.section-heading p').count(),0);
   assert.equal(await page.locator('.sound-controls').count(),0);
   await page.locator('#mode-multi').click();await page.waitForFunction(()=>document.querySelector('#mode-multi').getAttribute('aria-pressed')==='true');
   assert.match(await page.locator('#tracking-status').innerText(),/多人模式/);
   assert.equal(await page.locator('#tracking-frame').getAttribute('src'),null,'tracking is lazy');
   await page.screenshot({path:path.join(evidence,`monitor-${name}-fixture.png`),fullPage:true});
   await page.locator('[data-nav=tracking]').click();const tracking=page.frameLocator('#tracking-frame');
   await tracking.locator('#single').click();await tracking.locator('#mode').filter({hasText:'单人模式'}).waitFor();
   assert.equal(await tracking.locator('header').isVisible(),false,'embedded page shares shell');
   assert.equal(await tracking.locator('p.muted').count(),0);
   await page.screenshot({path:path.join(evidence,`tracking-${name}-fixture.png`),fullPage:true});
   await page.locator('[data-nav=care]').click();const control=page.frameLocator('#care-frame');
   await control.locator('#position').filter({hasText:'四点标定'}).waitFor();
   assert(await control.locator('#open').isDisabled());
   assert.equal(await control.locator('p.hint:not([id])').count(),0);
   assert.match(await control.locator('h2').first().innerText(),/远左 → 远右 → 近右 → 近左/);
   await control.locator('#neutral_us').fill('1510');
   await control.locator('#cancel').click();await control.locator('#notice').filter({hasText:'操作已应用'}).waitFor();
   assert(posts.some(post=>post.name==='/api/care/action'&&post.body==='cmd=cancel'));
   await page.screenshot({path:path.join(evidence,`care-${name}-fixture.png`),fullPage:true});
   await page.locator('[data-nav=device]').click();await page.waitForTimeout(2800);
   const before=calls['/api/care'];await page.waitForTimeout(1200);assert.equal(calls['/api/care'],before,'hidden care polling pauses');
   await page.locator('[data-nav=care]').click();assert.equal(await control.locator('#neutral_us').inputValue(),'1510','draft input preserved');
   await page.locator('[data-nav=events]').click();await page.waitForFunction(()=>document.querySelector('#archive-status').textContent.includes('共1条'));
   assert.equal(await page.locator('#archive-events .event-photo img').count(),1);
   await page.screenshot({path:path.join(evidence,`events-${name}-fixture.png`),fullPage:true});
   assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth>innerWidth),false);
   await page.locator('[data-nav=monitor]').click();await page.waitForFunction(()=>document.querySelector('#tracking-status').textContent.includes('单人模式'));
  }
  offline=true;await page.evaluate(()=>loadTrackingMode());assert(await page.locator('#mode-multi').isDisabled());assert.match(await page.locator('#tracking-status').innerText(),/中断/);
  offline=false;await page.evaluate(()=>loadTrackingMode());assert.equal(await page.locator('#mode-multi').isDisabled(),false);
  archiveOffline=true;await page.locator('[data-nav=events]').click();await page.waitForFunction(()=>document.querySelector('#archive-status').textContent.includes('启动05'));
  assert.equal(await page.locator('#archive-events .event-photo img').count(),0,'offline archive never shows stale photographs');
  assert.deepEqual(errors,[]);console.log('UNIFIED_FRONTEND_PASS desktop/mobile navigation, both mode controls, care cancel/form preservation, hidden polling pause, archive/photos, offline recovery, no JS errors; fixtures only. Screenshots: '+evidence);
 }finally{await browser.close();server.close()}
})().catch(error=>{console.error(error);server.close();process.exitCode=1});
