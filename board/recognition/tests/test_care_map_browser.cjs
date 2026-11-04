// Real page rendering and image decoding with fixture endpoints; not board calibration evidence.
const assert=require('node:assert/strict'),fs=require('node:fs'),http=require('node:http'),path=require('node:path'),os=require('node:os');
const {chromium}=require('C:/Users/xcy/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright');
const net=path.resolve(__dirname,'../software/src/net'),out=path.join(os.tmpdir(),'fpga-care-map-ui-20261102');fs.mkdirSync(out,{recursive:true});
const names=['厨房','卫生间','卧室','客厅'],rects=[[0,0,.5,.5],[.5,0,1,.5],[0,.5,.5,1],[.5,.5,1,1]],posts=[];
let frame=0,networkFail=false,boardFail=false;
const care={hardware:true,calibrated:false,valid:false,room:-1,wall:-1,age_ms:0,risk:0,armed:false,door_state:0,remaining_ms:0,countdown_ms:0,alarm:false,
 rooms:rects.map(rect=>({rect,lit:false})),trail_ages:Array(256).fill(-1),config:{brightness:3,leds_per_room:7,rotation:0,matrix_rgb:0,strip_rgb:0,serpentine:1,neutral_us:1500,open_us:1650,close_us:1350,run_ms:400}};
function image(res){res.setHeader('Content-Type','image/jpeg');res.setHeader('X-Frame-Id',String(++frame));res.end(fs.readFileSync(path.join(net,'media',frame%2?'demo-frame.jpg':'observer.png')));}
const server=http.createServer((req,res)=>{
 const url=req.url.split('?')[0],json=value=>{res.setHeader('Content-Type','application/json');res.end(JSON.stringify(value))};
 if(req.method==='POST'){let body='';req.on('data',b=>body+=b);req.on('end',()=>{posts.push({url,body});json({ok:true})});return;}
 if(url==='/api/care')return json(care);
 if(url==='/api/replay')return json({ready:false,buffered:0});
 if(url==='/privacy.js'){res.setHeader('Content-Type','application/javascript');return res.end(fs.readFileSync(path.join(net,'privacy.js')));}
 if(url==='/frame.bmp'){if(boardFail){res.statusCode=503;return res.end();}return image(res);}
 if(url==='/care.html'){res.setHeader('Content-Type','text/html;charset=utf-8');return res.end(fs.readFileSync(path.join(net,'care.html')));}
 res.statusCode=404;res.end();
});
(async()=>{
 await new Promise(r=>server.listen(0,'127.0.0.1',r));const browser=await chromium.launch({channel:'msedge',headless:true});
 try{
  const page=await browser.newPage(),errors=[];page.on('pageerror',e=>errors.push(e.message));
  await page.route('http://192.168.0.3:8093/api/frame.jpg**',async route=>{
   if(networkFail)return route.fulfill({status:503,body:''});
   const n=++frame;await route.fulfill({contentType:'image/jpeg',headers:{'X-Frame-Id':String(n)},body:fs.readFileSync(path.join(net,'media',n%2?'demo-frame.jpg':'observer.png'))});
  });
  for(const [name,width,height] of [['desktop',1366,1000],['mobile',390,844]]){
   await page.setViewportSize({width,height});await page.goto(`http://127.0.0.1:${server.address().port}/care.html?embedded=1`);
   await page.locator('#cameraStatus').filter({hasText:'画面已更新'}).waitFor();
   assert.deepEqual(await page.locator('#rooms .room').allTextContents(),names);
   assert.equal(await page.locator('#roomConfig tr').count(),5);
   assert.equal(await page.locator('#leds_per_room').inputValue(),'7');
   const before=await page.locator('#camera').evaluate(e=>e.toDataURL());
   await page.locator('#camera').click({position:{x:25,y:25}});assert.match(await page.locator('#pointInfo').textContent(),/1\/4/);
   await page.locator('#refresh').focus();await page.keyboard.press('Space');
   await page.locator('#cameraStatus').filter({hasText:'画面已更新'}).waitFor();
   assert.match(await page.locator('#pointInfo').textContent(),/0\/4/);
   assert.notEqual(await page.locator('#camera').evaluate(e=>e.toDataURL()),before);
   assert(await page.locator('#savePoints').isDisabled());
   await page.locator('#livePreview').focus();await page.keyboard.press('Space');
   await page.locator('#cameraStatus').filter({hasText:'实时预览中'}).waitFor();
   const previewAt=await page.evaluate(()=>cameraAttemptAt);
   await page.waitForFunction(t=>cameraAttemptAt>t+250,previewAt);
   await page.locator('#camera').click({position:{x:25,y:25}});
   assert.equal(await page.locator('#livePreview').isChecked(),false);
   assert.match(await page.locator('#pointInfo').textContent(),/1\/4/);
   const paused=await page.locator('#camera').evaluate(e=>e.toDataURL());
   await page.waitForFunction(()=>!cameraLoading);
   assert.equal(await page.locator('#camera').evaluate(e=>e.toDataURL()),paused,'A late preview frame must not replace the frozen calibration image');
   await page.locator('summary').click();await page.locator('#saveLayout').click();
   await page.locator('#notice').filter({hasText:'操作已应用'}).waitFor();
   const body=new URLSearchParams(posts.at(-1).body);assert.equal(body.get('r30'),'0.5');assert.equal(body.get('r33'),'1');
   assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth));
   care.alarm=true;care.rooms.forEach(r=>r.lit=true);
   await page.locator('#rescue').filter({hasText:'全部照明灯红闪'}).waitFor();
   assert.equal(await page.locator('#lighting').textContent(),'照明：四面墙红色闪烁');
   assert.equal(await page.locator('#rooms .alarm').count(),4);
   await page.screenshot({path:path.join(out,name+'.png'),fullPage:true});
   care.alarm=false;care.rooms.forEach(r=>r.lit=false);
  }
  networkFail=true;await page.locator('#refresh').click();await page.locator('#cameraStatus').filter({hasText:'画面已更新'}).waitFor();
  boardFail=true;await page.locator('#refresh').click();await page.locator('#cameraStatus').filter({hasText:'取图失败'}).waitFor();
  assert(await page.locator('#refresh').isEnabled());
  networkFail=boardFail=false;await page.locator('#refresh').click();await page.locator('#cameraStatus').filter({hasText:'画面已更新'}).waitFor();
  assert.deepEqual(errors,[]);console.log('CARE_MAP_BROWSER_PASS: desktop/mobile four rooms, changing decoded image, keyboard refresh and point reset, r3 form, BMP fallback, failure/recovery; fixtures only. '+out);
 }finally{await browser.close();server.close();}
})().catch(e=>{console.error(e);process.exitCode=1;server.close();});

