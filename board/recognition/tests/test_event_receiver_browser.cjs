const assert=require('node:assert/strict');
const fs=require('node:fs');
const http=require('node:http');
const path=require('node:path');
const {chromium}=require('C:/Users/xcy/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright');
const root=path.resolve(__dirname,'../..');
const html=fs.readFileSync(path.join(root,'tools/event_receiver.html'));
const bmp=Buffer.alloc(1078+96*54);bmp.write('BM');bmp.writeUInt32LE(bmp.length,2);bmp.writeUInt32LE(1078,10);bmp.writeUInt32LE(40,14);bmp.writeInt32LE(96,18);bmp.writeInt32LE(-54,22);bmp.writeUInt16LE(1,26);bmp.writeUInt16LE(8,28);
for(let i=0;i<256;i++)for(let c=0;c<3;c++)bmp[54+i*4+c]=i;
for(let i=1078;i<bmp.length;i++)bmp[i]=i%251;
let mode='empty';
const event={id:1,track:3,state:2,uptime_ms:12345,frame:77,detector:850,rule:700,score:780,weighted:1,x0:100,y0:80,x1:900,y1:1000,session:'00112233445566778899aabbccddeeff',received_utc:'2026-11-01T09:00:00Z'};
const server=http.createServer((req,res)=>{
 if(req.url.startsWith('/api/events')){res.setHeader('Content-Type','application/json');if(mode==='error'){res.writeHead(503);return res.end('{}');}return res.end(JSON.stringify({connected:mode==='full',count:mode==='full'?1:0,connections:mode==='full'?1:0,last_contact:mode==='full'?event.received_utc:null,duplicates:0,export_error:null,events:mode==='full'?[event]:[]}));}
 if(req.url.startsWith('/image/')){res.setHeader('Content-Type','image/bmp');return res.end(bmp);}
 res.setHeader('Content-Type','text/html;charset=utf-8');res.end(html);
});
(async()=>{
 await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
 const browser=await chromium.launch({channel:'msedge',headless:true});
 try{
  const page=await browser.newPage();const errors=[];page.on('pageerror',error=>errors.push(error.message));
  for(const [name,width,height] of [['desktop',1365,900],['mobile',390,844]]){
   mode='empty';await page.setViewportSize({width,height});await page.goto(`http://127.0.0.1:${server.address().port}/`);await page.waitForFunction(()=>document.querySelector('#events').textContent.includes('还没有收到'));
   assert.match(await page.locator('#connection').innerText(),/等待板卡/);
   mode='full';await page.evaluate(()=>refresh());await page.waitForFunction(()=>document.querySelectorAll('article').length===1);
   await page.waitForFunction(()=>document.querySelector('article img').complete&&document.querySelector('article img').naturalWidth===96);
   assert.match(await page.locator('article').innerText(),/85.0%.*0.700.*0.780/);
   assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth>innerWidth),false);
   assert.equal(await page.locator('nav a').count(),3);
   await page.evaluate(()=>document.querySelector('h1').textContent+=' · 离线测试数据');
   await page.screenshot({path:path.join(root,`validation/ethernet-ui-${name}-fixture.png`),fullPage:true});
  }
  mode='error';await page.evaluate(()=>refresh());await page.waitForFunction(()=>document.querySelector('#connection').textContent.includes('接收服务不可用'));
  assert.deepEqual(errors,[]);
  console.log('EVENT_RECEIVER_BROWSER_PASS desktop/mobile, real empty state, fixture fields/image, no overflow, server error, no JS errors');
 }finally{await browser.close();server.close();}
})().catch(error=>{console.error(error);server.close();process.exitCode=1;});
