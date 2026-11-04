// Browser plugin not available; installed Playwright validates synthetic events only.
const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path'),http=require('node:http');
const {chromium}=require('C:/Users/xcy/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright');
const root=path.resolve(__dirname,'../..'),net=path.join(root,'recognition/software/src/net');
const session='00112233445566778899aabbccddeeff';
const event={id:1,track:19,state:2,uptime_ms:445651,frame:13364,rule:756};
const bmp=Buffer.alloc(54+96*54*3);bmp.write('BM');bmp.writeUInt32LE(bmp.length,2);bmp.writeUInt32LE(54,10);bmp.writeUInt32LE(40,14);bmp.writeInt32LE(96,18);bmp.writeInt32LE(-54,22);bmp.writeUInt16LE(1,26);bmp.writeUInt16LE(24,28);
for(let i=54;i<bmp.length;i+=3){bmp[i]=3;bmp[i+1]=17;bmp[i+2]=255;}
let boot=session,mode='saved';
const server=http.createServer((req,res)=>{
 const name=req.url.split('?')[0];
 if(name==='/api/events'||name==='/api/report'){res.setHeader('Content-Type','application/json');return res.end(JSON.stringify(name==='/api/events'?{volatile:true,events:[event]}:{session:boot}));}
 if(name.startsWith('/api/')||name==='/frame.bmp'){res.writeHead(503);return res.end('{}');}
 const file=path.join(net,name==='/'?'board.html':name);
 if(!fs.existsSync(file)){res.writeHead(404);return res.end();}
 res.setHeader('Content-Type',name.endsWith('.js')?'application/javascript':name.endsWith('.css')?'text/css':'text/html;charset=utf-8');res.end(fs.readFileSync(file));
});
(async()=>{
 await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
 const browser=await chromium.launch({channel:'msedge',headless:true});
 try{
  const page=await browser.newPage(),errors=[];page.on('pageerror',e=>errors.push(e.message));
  await page.route('http://192.168.0.3:8093/**',r=>r.abort());
  await page.route('http://192.168.0.3:8092/**',r=>{
   if(mode==='offline')return r.abort();
   if(r.request().url().includes('/image/'))return r.fulfill({contentType:'image/bmp',body:bmp});
   return r.fulfill({contentType:'application/json',body:JSON.stringify({events:mode==='pending'?[]:[{...event,session,color:true,frame:mode==='wrong-frame'?99:event.frame}]})});
  });
  for(const [name,width,height] of [['desktop',1366,950],['mobile',390,844]]){
   await page.setViewportSize({width,height});await page.goto(`http://127.0.0.1:${server.address().port}/?snapshot=1#events`);
   await page.waitForFunction(()=>document.querySelector('#events .event-photo img')?.naturalWidth===96);
   assert.equal(await page.locator('#events figcaption').count(),0);
   assert.equal(await page.locator('#events .event-photo').getAttribute('href'),`http://192.168.0.3:8092/image/${session}/1.bmp`);
   assert.equal(await page.locator('#events .event-photo img').evaluate(img=>getComputedStyle(img).objectFit),'contain');
   assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth>innerWidth),false);
   await page.screenshot({path:path.join(root,`validation/color-snapshot-${name}-fixture.png`),fullPage:true});
  }
  for(const scenario of ['pending','wrong-frame','reboot','offline']){
   mode=scenario;boot=scenario==='reboot'?'ffeeddccbbaa99887766554433221100':session;
   await page.evaluate(()=>loadEvents());
   assert.equal(await page.locator('#events .event-photo img').count(),0,scenario+' must not show wrong/stale evidence');
   assert.match(await page.locator('#events .event-evidence').innerText(),/暂无抓拍/);
  }
  assert.equal(await page.evaluate(()=>safeSnapshotUrl('http://evil.example/image/a.bmp')),'');
  assert.deepEqual(errors,[]);
  console.log('EVENT_PHOTOS_BROWSER_PASS desktop/mobile RGB image, full picture, pending/offline/reboot/wrong-frame protection, URL validation, no overflow or JS errors; fixture only');
 }finally{await browser.close();server.close();}
})().catch(e=>{console.error(e);server.close();process.exitCode=1;});
