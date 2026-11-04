// Browser plugin not available; use installed Playwright. Synthetic video proves UI only.
const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path'),http=require('node:http');
const {chromium}=require('C:/Users/xcy/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright');
const root=path.resolve(__dirname,'../..'),net=path.join(root,'recognition/software/src/net');
const jpeg=fs.readFileSync(path.join(net,'media/demo-frame.jpg'));let online=true,streams=0,frames=0,bmp=0,lowPower=false;
const status={server:'riscv-lwip',uptime_ms:10000,capture_us:9900000,period_ms:500,tracks:[],alarm_latched:0,rx_bad:0,rx_dropped:0,sensors:{env_valid:1,ambient:2500,humidity:5000,thermal_valid:1,heat:Array(768).fill(2500),age_us:100000,thermal_frames:50}};
const server=http.createServer((req,res)=>{
 const name=req.url.split('?')[0];
 if(name==='/api/video'){res.setHeader('Content-Type','application/json');return res.end(JSON.stringify({source:'v6-ethernet',connected:online,fps:online?10:0,url:online?'http://192.168.0.3:8093/api/stream.mjpg':'',frame_id:frames}));}
 if(name==='/api/stream.mjpg'){
  streams++;res.setHeader('Content-Type','multipart/x-mixed-replace; boundary=frame');
  const send=()=>{frames++;res.write(`--frame\r\nContent-Type: image/jpeg\r\nContent-Length: ${jpeg.length}\r\nX-Frame-Id: ${frames}\r\n\r\n`);res.write(jpeg);res.write('\r\n');};send();const timer=setInterval(send,100);req.on('close',()=>clearInterval(timer));return;
 }
 if(name==='/api/status'||name==='/api/events'){res.setHeader('Content-Type','application/json');return res.end(JSON.stringify(name==='/api/status'?status:{events:[]}));}
 if(name==='/frame.bmp'){bmp++;res.writeHead(500);return res.end();}
 const file=path.join(net,name==='/'?'board.html':name);
 if(!fs.existsSync(file)){res.writeHead(404);return res.end();}
 res.setHeader('Content-Type',name.endsWith('.js')?'application/javascript':name.endsWith('.css')?'text/css':name.endsWith('.png')?'image/png':'text/html;charset=utf-8');res.end(fs.readFileSync(file));
});
(async()=>{
 await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));const base=`http://127.0.0.1:${server.address().port}`;
 const browser=await chromium.launch({channel:'msedge',headless:true});
 try{
  const context=await browser.newContext();const page=await context.newPage();const errors=[];page.on('pageerror',e=>errors.push(e.message));
  // Cross-origin board -> PC status is CORS-enabled in production; route fixtures locally.
  await page.route('http://192.168.0.3:8093/api/video',async route=>route.fulfill({status:200,contentType:'application/json',body:JSON.stringify({source:'v6-ethernet',connected:online,fps:online?(lowPower?2:10):0,low_power:lowPower,url:online?base+'/api/stream.mjpg':'',frame_id:frames})}));
  for(const [name,width,height] of [['desktop',1366,950],['mobile',390,844]]){
   await page.setViewportSize({width,height});await page.goto(base+'/?snapshot=1');
   await page.evaluate(async()=>{await loadEthernetVideo();await loadStatus();});
   await page.waitForFunction(()=>document.querySelector('#video-state').textContent.includes('10.0'));
   await page.waitForFunction(()=>document.querySelector('#stream-image').naturalWidth>0);
   const before=frames;await page.waitForTimeout(450);assert(frames>=before+3,'continuous distinct multipart frames');
   assert.equal(await page.locator('#video-note').count(),0);
   assert.equal(await page.locator('#fps').textContent(),'2.0');
   assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth>innerWidth),false);
   await page.evaluate(()=>document.title+=' · 离线测试数据');
   await page.screenshot({path:path.join(root,`validation/network-video-ui-${name}-fixture.png`),fullPage:true});
   lowPower=true;await page.evaluate(()=>loadEthernetVideo());
   assert.match(await page.locator('#video-state').innerText(),/低功耗画面.*2.0 帧/);
   lowPower=false;await page.evaluate(()=>loadEthernetVideo());
   assert.match(await page.locator('#video-state').innerText(),/网口连续画面.*10.0 帧/);
   online=false;await page.evaluate(()=>loadEthernetVideo());await page.waitForFunction(()=>document.querySelector('#video-state').textContent.includes('等待网口'));
   online=true;await page.evaluate(()=>loadEthernetVideo());await page.waitForFunction(()=>document.querySelector('#video-state').textContent.includes('10.0'));
   await page.evaluate(async()=>{location.hash='#device';await loadEthernetVideo();});
   assert.equal(await page.locator('#stream-image').getAttribute('src'),null);
   await page.evaluate(async()=>{location.hash='';await loadEthernetVideo();});
   await page.waitForFunction(()=>document.querySelector('#stream-image').getAttribute('src'));
  }
  assert.equal(bmp,0,'old slow BMP polling must remain stopped');assert(streams>=4,'reconnection opens a fresh MJPEG stream');assert.deepEqual(errors,[]);
  console.log('ETHERNET_VIDEO_BROWSER_PASS board UI -> PC real multipart fixture; distinct frames, measured label separated from detection, desktop/mobile, reconnect, zero BMP polling; hardware not tested');
 }finally{await browser.close();server.close();}
})().catch(error=>{console.error(error);server.close();process.exitCode=1;});
